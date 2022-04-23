
#include "simtool-script-generator.h"
#include "dataobj/environment.h"
#include "descriptor/building_desc.h"
#include "descriptor/ground_desc.h"
#include "gui/messagebox.h"
#include "gui/script_generator_frame.h"
#include "gui/simwin.h"
#include "obj/wayobj.h"
#include "obj/gebaeude.h"
#include "obj/signal.h"
#include "obj/zeiger.h"
#include "dataobj/koord3d.h"
#include "sys/simsys.h"


#define dr_fopen fopen

void tool_generate_script_t::mark_tiles(  player_t *, const koord3d &start, const koord3d &end )
{
	koord k1, k2;
	k1.x = start.x < end.x ? start.x : end.x;
	k1.y = start.y < end.y ? start.y : end.y;
	k2.x = start.x + end.x - k1.x;
	k2.y = start.y + end.y - k1.y;
	koord k;
	for(  k.x = k1.x;  k.x <= k2.x;  k.x++  ) {
		for(  k.y = k1.y;  k.y <= k2.y;  k.y++  ) {
			grund_t *gr = welt->lookup( koord3d(k.x, k.y, start.z) );
      if(  !gr  ) {
        continue;
      }
			zeiger_t *marker = new zeiger_t(gr->get_pos(), NULL );
      
      const uint8 grund_hang = gr->get_grund_hang();
			const uint8 weg_hang = gr->get_weg_hang();
			const uint8 hang = max( corner_sw(grund_hang), corner_sw(weg_hang)) +
					3 * max( corner_se(grund_hang), corner_se(weg_hang)) +
					9 * max( corner_ne(grund_hang), corner_ne(weg_hang)) +
					27 * max( corner_nw(grund_hang), corner_nw(weg_hang));
			uint8 back_hang = (hang % 3) + 3 * ((uint8)(hang / 9)) + 27;
			marker->set_foreground_image( ground_desc_t::marker->get_image( grund_hang % 27 ) );
			marker->set_image( ground_desc_t::marker->get_image( back_hang ) );
      marker->mark_image_dirty( marker->get_image(), 0 );
			gr->obj_add( marker );
			marked.insert( marker );
		}
	}
}


void write_station_at(cbuffer_t &buf, const koord3d pos, const koord3d origin) {
  const grund_t* gr = world()->lookup(pos);
  const gebaeude_t* obj = gr ? obj_cast<gebaeude_t>(gr->first_obj()) : NULL;
  const building_desc_t* desc = obj ? obj->get_tile()->get_desc() : NULL;
  if(  !desc  ||  desc->get_type()!=building_desc_t::generic_stop  ) {
    return;
  }
  // now this pos has a stop.
  koord3d diff = pos - origin;
  buf.printf("\thm_station_tl(\"%s\",[%d,%d,%d])\n", desc->get_name(), diff.x, diff.y, diff.z);
}


void write_sign_at(cbuffer_t &buf, const koord3d pos, const koord3d origin) {
  const grund_t* gr = world()->lookup(pos);
	const weg_t* weg = gr ? gr->get_weg_nr(0) : NULL;
	if(  !weg  ||  (!weg->has_sign()  &&  !weg->has_signal())  ) {
		return;
	}
	// now this tile should have a sign.
	roadsign_t* sign = NULL;
	if(  signal_t* s = gr->find<signal_t>()  ) {
		sign = s;
	} else {
		// a sign should exists.
		sign = gr->find<roadsign_t>();
	}
	if(  sign  ) {
		// now this pos has a stop.
		uint8 cnt = 1;
		const ribi_t::ribi d = sign->get_dir();
		const bool ow = sign->get_desc()->is_single_way();
		if(  !ribi_t::is_single(d)  ) {
			cnt = 1;
		}
		else if(  ribi_t::is_straight(weg->get_ribi_unmasked())  ) {
			cnt = (d==ribi_t::north  ||  d==ribi_t::east) ^ ow ? 2 : 3;
		} else if(  ribi_t::is_bend(weg->get_ribi_unmasked())  ) {
			cnt = (d==ribi_t::north  ||  d==ribi_t::south) ^ ow ? 2 : 3;
		}
	  koord3d diff = pos - origin;
	  buf.printf("\thm_sign_tl(\"%s\",%d,[%d,%d,%d])\n", sign->get_desc()->get_name(), cnt, diff.x, diff.y, diff.z);
	}
}


void write_slope_at(cbuffer_t &buf, const koord3d pos, const koord3d origin) {
  const grund_t* gr = world()->lookup(pos);
  if(  !gr  ||  !gr->ist_karten_boden()  ) {
    return;
  }
  const koord3d pb = pos - origin;
  sint8 diff = pb.z;
  while(  diff!=0  ) {
    if(  diff>0  ) {
      // raise the land
      buf.printf("\thm_slope_tl(hm_slope.UP,[%d,%d,%d])\n", pb.x, pb.y, pb.z-diff);
      diff -= 1;
    } else  {
      // lower the land
      buf.printf("\thm_slope_tl(hm_slope.DOWN,[%d,%d,%d])\n", pb.x, pb.y, pb.z-diff);
      diff += 1;
    }
  }
  // check slopes
  const slope_t::type slp = gr->get_weg_hang();
  if(  slp>0  ) {
    buf.printf("\thm_slope_tl(%d,[%d,%d,%d])\n", slp, pb.x, pb.y, pb.z);
  }
}


void write_command(cbuffer_t &buf, void (*func)(cbuffer_t &, const koord3d, const koord3d), const koord start, const koord end, const koord3d origin) {
  for(sint8 z=-128;  z<127;  z++) { // iterate for all height
    for(sint16 x=start.x;  x<=end.x;  x++) {
      for(sint16 y=start.y;  y<=end.y;  y++) {
        func(buf, koord3d(x, y, z), origin);
      }
    }
  }
}


// for functions which need concatenation
class write_path_command_t {
protected:
	struct {
		koord3d start;
		koord3d end;
		const char* desc_name;
	}typedef script_cmd;
	vector_tpl<script_cmd> commands;
	cbuffer_t& buf;
	const char* cmd_str;
	koord start, end;
	koord3d origin;
	virtual void append_command(koord3d pos, const ribi_t::ribi(&dirs)[2]) = 0;
	virtual bool can_concatnate(script_cmd& a, script_cmd& b) = 0;
	
public:
	write_path_command_t(cbuffer_t& b,koord s, koord e, koord3d o ) :
	buf(b), start(s), end(e), origin(o) { };
	
	void write() {
	  for(sint8 z=-128;  z<127;  z++) { // iterate for all height
	    for(sint16 x=start.x;  x<=end.x;  x++) {
	      for(sint16 y=start.y;  y<=end.y;  y++) {
					ribi_t::ribi dirs[2];
					dirs[0] = x>start.x ? ribi_t::west : ribi_t::none;
					dirs[1] = y>start.y ? ribi_t::north : ribi_t::none;
					append_command(koord3d(x, y, z), dirs);
	      }
	    }
	  }
		// concatenate the command
		while(  !commands.empty()  ) {
			script_cmd cmd = commands.pop_back();
			bool adjacent_found = true;
			while(  adjacent_found  ) {
				adjacent_found = false;
				for(uint32 i=0;  i<commands.get_count();  i++) {
					if(  !can_concatnate(commands[i], cmd)  ) {
						continue;
					}
					if(  cmd.end==commands[i].start  ) {
						cmd.end = commands[i].end;
						adjacent_found = true;
					} else if(  cmd.start==commands[i].end  ) {
						cmd.start = commands[i].start;
						adjacent_found = true;
					}
					if(  adjacent_found  ) {
						commands.remove_at(i);
						break;
					}
				}
			}
			// all adjacent entries were concatenated.
			buf.printf("\t%s(\"%s\",[%d,%d,%d],[%d,%d,%d])\n", cmd_str, cmd.desc_name, cmd.start.x, cmd.start.y, cmd.start.z, cmd.end.x, cmd.end.y, cmd.end.z);
		}
	}
};

class write_way_command_t : public write_path_command_t {
protected:
	void append_command(koord3d pos, const ribi_t::ribi(&dirs)[2]) OVERRIDE {
		const grund_t* gr = world()->lookup(pos);
	  const weg_t* weg0 = gr ? gr->get_weg_nr(0) : NULL;
	  if(  !weg0  ) {
	    return;
	  }
	  koord3d pb = pos - origin; // relative base pos
		if(  gr->get_typ()==grund_t::monorailboden  ) {
			pb.z -= world()->get_settings().get_way_height_clearance();
		}
	  for(uint8 i=0;  i<2;  i++) {
			if(  dirs[i]==ribi_t::none  ) {
				continue;
			}
	    grund_t* to = NULL;
	    gr->get_neighbour(to, weg0->get_waytype(), dirs[i]);
	    if(  to  &&  to->get_typ()==gr->get_typ()  ) {
	      koord3d tp = to->get_pos()-origin;
	      if(  to->get_typ()==grund_t::monorailboden  ) {
	        tp = tp - koord3d(0,0,world()->get_settings().get_way_height_clearance());
	      }
				commands.append(script_cmd{pb, tp, to->get_weg_nr(0)->get_desc()->get_name()});
	    }
	  }
	}
	
	bool can_concatnate(script_cmd& a, script_cmd& b) OVERRIDE {
		return strcmp(a.desc_name, b.desc_name)==0  &&
			ribi_type(a.start, a.end)==ribi_type(b.start, b.end);
	}
	
public:
	write_way_command_t(cbuffer_t& b,koord s, koord e, koord3d o ) :
	write_path_command_t(b, s, e, o)
	{ 
		cmd_str = "hm_way_tl";
	}
};


class write_wayobj_command_t : public write_path_command_t {
protected:
	void append_command(koord3d pos, const ribi_t::ribi(&dirs)[2]) OVERRIDE {
		const grund_t* gr = world()->lookup(pos);
	  const weg_t* weg0 = gr ? gr->get_weg_nr(0) : NULL;
		const wayobj_t* wayobj = weg0 ? gr->get_wayobj(weg0->get_waytype()) : NULL;
	  if(  !wayobj  ) {
	    return;
	  }
		const waytype_t type = weg0->get_waytype();
	  for(uint8 i=0;  i<2;  i++) {
			if(  dirs[i]==ribi_t::none  ) {
				continue;
			}
	    grund_t* to = NULL;
	    gr->get_neighbour(to, type, dirs[i]);
			const wayobj_t* t_obj = to ? to->get_wayobj(type) : NULL;
	    if(  t_obj   &&  t_obj->get_desc()==wayobj->get_desc()  ) {
				commands.append(script_cmd{pos-origin, to->get_pos()-origin, wayobj->get_desc()->get_name()});
	    }
	  }
	}
	
	bool can_concatnate(script_cmd& a, script_cmd& b) OVERRIDE {
		return strcmp(a.desc_name, b.desc_name)==0;
	}
	
public:
	write_wayobj_command_t(cbuffer_t& b,koord s, koord e, koord3d o ) :
	write_path_command_t(b, s, e, o)
	{ 
		cmd_str = "hm_wayobj_tl";
	}
};


char const* tool_generate_script_t::do_work(player_t* , const koord3d &start, const koord3d &end) {
  koord3d e = end==koord3d::invalid ? start : end;
  koord k1 = koord(min(start.x, e.x), min(start.y, e.y));
  koord k2 = koord(max(start.x, e.x), max(start.y, e.y));
  buf.clear();
  buf.append("include(\"hm_toolkit_v1\")\n\nfunction hm_build() {\n"); // header
  
  write_command(buf, write_slope_at, k1, k2, start);
	write_way_command_t(buf, k1, k2, start).write();
	write_wayobj_command_t(buf, k1, k2, start).write();
	write_command(buf, write_sign_at, k1, k2, start);
  write_command(buf, write_station_at, k1, k2, start);
  
  buf.append("}\n"); // footer
  create_win(new script_generator_frame_t(this), w_info, magic_script_generator);
  return NULL;
}


bool tool_generate_script_t::save_script(const char* fullpath) const {
	cbuffer_t dir_buf;
	dir_buf.printf("%sgenerated-scripts", env_t::data_dir);
	dr_mkdir(dir_buf.get_str());
  FILE* file;
  file = dr_fopen(fullpath, "w");
  if(  file==NULL  ) {
    dbg->error("tool_generate_script_t::save_script()", "cannot save file %s", fullpath);
		create_win( new news_img("The script cannot be saved!\n"), w_time_delete, magic_none);
    return false;
  }
  fprintf(file, "%s", buf.get_str());
  fclose(file);
	create_win( new news_img("The generated script was saved!\n"), w_time_delete, magic_none);
  return true;
}
