/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "weg.h"

#include "schiene.h"
#include "strasse.h"
#include "monorail.h"
#include "maglev.h"
#include "narrowgauge.h"
#include "kanal.h"
#include "runway.h"


#include "../grund.h"
#include "../../simworld.h"
#include "../../display/simimg.h"
#include "../../simhalt.h"
#include "../../obj/simobj.h"
#include "../../player/simplay.h"
#include "../../obj/roadsign.h"
#include "../../obj/signal.h"
#include "../../obj/wayobj.h"
#include "../../obj/crossing.h"
#include "../../utils/cbuffer_t.h"
#include "../../dataobj/environment.h" // TILE_HEIGHT_STEP
#include "../../dataobj/translator.h"
#include "../../dataobj/loadsave.h"
#include "../../descriptor/way_desc.h"
#include "../../descriptor/roadsign_desc.h"

#include "../../tpl/slist_tpl.h"

#ifdef MULTI_THREAD
#include "../../utils/simthread.h"
static pthread_mutex_t weg_calc_image_mutex;
static recursive_mutex_maker_t weg_cim_maker(weg_calc_image_mutex);
#endif

/**
 * Alle instantiierten Wege
 */
slist_tpl <weg_t *> alle_wege;

/**
 * Get list of all ways
 */
const slist_tpl <weg_t *> & weg_t::get_alle_wege()
{
	return alle_wege;
}


// returns a way with matching waytype
weg_t* weg_t::alloc(waytype_t wt)
{
	weg_t *weg = NULL;
	switch(wt) {
		case tram_wt:
		case track_wt:
			weg = new schiene_t();
			break;
		case monorail_wt:
			weg = new monorail_t();
			break;
		case maglev_wt:
			weg = new maglev_t();
			break;
		case narrowgauge_wt:
			weg = new narrowgauge_t();
			break;
		case road_wt:
			weg = new strasse_t();
			break;
		case water_wt:
			weg = new kanal_t();
			break;
		case air_wt:
			weg = new runway_t();
			break;
		default:
			// keep compiler happy; should never reach here anyway
			assert(0);
			break;
	}
	return weg;
}


// returns a string with the "official name of the waytype"
const char *weg_t::waytype_to_string(waytype_t wt)
{
	switch(wt) {
		case tram_wt:        return "tram_track";
		case track_wt:       return "track";
		case monorail_wt:    return "monorail_track";
		case maglev_wt:      return "maglev_track";
		case narrowgauge_wt: return "narrowgauge_track";
		case road_wt:        return "road";
		case water_wt:       return "water";
		case air_wt:         return "air_wt";
		default:
			// keep compiler happy; should never reach here anyway
			break;
	}
	return "invalid waytype";
}


void weg_t::set_desc(const way_desc_t *b)
{
	desc = b;

	if(  hat_gehweg() &&  desc->get_wtyp() == road_wt  &&  desc->get_topspeed() > 50  ) {
		max_speed = 50;
	}
	else {
		max_speed = desc->get_topspeed();
	}
}


/**
 * initializes statistic array
 */
void weg_t::init_statistics()
{
	for(  int type=0;  type<MAX_WAY_STATISTICS;  type++  ) {
		for(  int month=0;  month<MAX_WAY_STAT_MONTHS;  month++  ) {
			statistics[month][type] = 0;
		}
	}
}


/**
 * Initializes all member variables
 */
void weg_t::init()
{
	ribi = ribi_maske = ribi_t::none;
	max_speed = 450;
	desc = 0;
	init_statistics();
	alle_wege.insert(this);
	flags = 0;
	image = IMG_EMPTY;
	foreground_image = IMG_EMPTY;
	max_wayobj_speed = 0;
}


weg_t::~weg_t()
{
	alle_wege.remove(this);
	player_t *player=get_owner();
	if(player) {
		player_t::add_maintenance( player,  -desc->get_maintenance(), desc->get_finance_waytype() );
	}
}


void weg_t::rdwr(loadsave_t *file)
{
	xml_tag_t t( file, "weg_t" );

	// save owner
	if(  file->is_version_atleast(99, 6)  ) {
		sint8 spnum=get_owner_nr();
		file->rdwr_byte(spnum);
		set_owner_nr(spnum);
	}

	// all connected directions
	uint8 dummy8 = ribi;
	file->rdwr_byte(dummy8);
	if(  file->is_loading()  ) {
		ribi = dummy8 & 15; // before: high bits was maske
		ribi_maske = 0; // maske will be restored by signal/roadsing
	}

	uint16 dummy16=max_speed;
	file->rdwr_short(dummy16);
	max_speed=dummy16;

	if(  file->is_version_atleast(89, 0)  ) {
		dummy8 = flags;
		file->rdwr_byte(dummy8);
		if(  file->is_loading()  ) {
			// all other flags are restored afterwards
			flags = dummy8 & HAS_SIDEWALK;
		}
	}

	for(  int type=0;  type<MAX_WAY_STATISTICS;  type++  ) {
		for(  int month=0;  month<MAX_WAY_STAT_MONTHS;  month++  ) {
			sint32 w = statistics[month][type];
			file->rdwr_long(w);
			statistics[month][type] = (sint16)w;
			// DBG_DEBUG("weg_t::rdwr()", "statistics[%d][%d]=%d", month, type, statistics[month][type]);
		}
	}
}


void weg_t::info(cbuffer_t & buf) const
{
	obj_t::info(buf);

	buf.printf("%s %u%s", translator::translate("Max. speed:"), max_speed, translator::translate("km/h\n"));
	if( get_waytype() == track_wt && max_wayobj_speed ){
		buf.printf("%s %u%s", translator::translate("Max. wayobj speed:"), max_wayobj_speed, translator::translate("km/h\n"));
	}
	buf.printf("%s%u",    translator::translate("\nRibi (unmasked)"), get_ribi_unmasked());
	buf.printf("%s%u\n",  translator::translate("\nRibi (masked)"),   get_ribi());

	if(  get_waytype() == road_wt  ) {
		const strasse_t* str = (const strasse_t*) this;
		assert(str);
		// Display overtaking_info
		switch (str->get_overtaking_mode()) {
			case halt_mode:
				buf.printf("%s %s\n", translator::translate("Overtaking:"),translator::translate("halt mode"));
				break;
			case oneway_mode:
				buf.printf("%s %s\n", translator::translate("Overtaking:"),translator::translate("oneway"));
				break;
			case twoway_mode:
				buf.printf("%s %s\n", translator::translate("Overtaking:"),translator::translate("twoway"));
				break;
			case loading_only_mode:
				buf.printf("%s %s\n", translator::translate("Overtaking:"),translator::translate("only loading convoi"));
				break;
			case prohibited_mode:
				buf.printf("%s %s\n", translator::translate("Overtaking:"),translator::translate("prohibited"));
				break;
			case inverted_mode:
				buf.printf("%s %s\n", translator::translate("Overtaking:"),translator::translate("inverted"));
				break;
			default:
				buf.printf("%s %s %d\n", translator::translate("Overtaking:"),translator::translate("ERROR"),str->get_overtaking_mode());
				break;
		}

		grund_t* gr = welt->lookup(get_pos());

		if(  gr  &&  !gr->ist_tunnel()  &&  !gr->ist_bruecke()  &&  desc->get_styp()==0  &&  !hat_gehweg()  ) {
			// only display this when this way is ground way.
			buf.printf("%s %s\n", translator::translate("Can be cityroad:"),str->get_avoid_cityroad()?translator::translate("No"):translator::translate("Yes"));
		}

		if(  str->get_citycar_no_entry()  ) {
			buf.printf("%s\n", translator::translate("Citycars are excluded."));
		}
	}

	if(has_sign()) {
		buf.append(translator::translate("\nwith sign/signal\n"));
	}

	if(is_electrified()) {
		buf.append(translator::translate("\nelektrified"));
	}
	else {
		buf.append(translator::translate("\nnot elektrified"));
	}

#if 1
	buf.printf(translator::translate("convoi passed last\nmonth %i\n"), statistics[1][1]);
#else
	// Debug - output stats
	buf.append("\n");
	for (int type=0; type<MAX_WAY_STATISTICS; type++) {
		for (int month=0; month<MAX_WAY_STAT_MONTHS; month++) {
			buf.printf("%d ", statistics[month][type]);
		}
	buf.append("\n");
	}
#endif
	buf.append("\n");
	if (char const* const maker = get_desc()->get_copyright()) {
		buf.printf(translator::translate("Constructed by %s"), maker);
		buf.append("\n");
	}
}


/**
 * called during map rotation
 */
void weg_t::rotate90()
{
	obj_t::rotate90();
	ribi = ribi_t::rotate90( ribi );
	ribi_maske = ribi_t::rotate90( ribi_maske );
}


/**
 * counts signals on this tile;
 * It would be enough for the signals to register and unregister themselves, but this is more secure ...
 */
void weg_t::count_sign()
{
	// Either only sign or signal please ...
	flags &= ~(HAS_SIGN|HAS_SIGNAL|HAS_CROSSING);
	const grund_t *gr=welt->lookup(get_pos());
	if(gr) {
		uint8 i = 1;
		// if there is a crossing, the start index is at least three ...
		if(  gr->ist_uebergang()  ) {
			flags |= HAS_CROSSING;
			i = 3;
			const crossing_t* cr = gr->find<crossing_t>();
			uint32 top_speed = cr->get_desc()->get_maxspeed( cr->get_desc()->get_waytype(0)==get_waytype() ? 0 : 1);
			if(  top_speed < max_speed  ) {
				max_speed = top_speed;
			}
		}
		// since way 0 is at least present here ...
		for( ;  i<gr->get_top();  i++  ) {
			obj_t *obj=gr->obj_bei(i);
			// sign for us?
			if(  roadsign_t const* const sign = obj_cast<roadsign_t>(obj)  ) {
				if(  sign->get_desc()->get_wtyp() == get_desc()->get_wtyp()  ) {
					// here is a sign ...
					flags |= HAS_SIGN;
					return;
				}
			}
			if(  signal_t const* const signal = obj_cast<signal_t>(obj)  ) {
				if(  signal->get_desc()->get_wtyp() == get_desc()->get_wtyp()  ) {
					// here is a signal ...
					flags |= HAS_SIGNAL;
					return;
				}
			}
		}
	}
}


void weg_t::set_images(image_type typ, uint8 ribi, bool snow, bool switch_nw)
{
	switch(typ) {
		case image_flat:
		default:
			set_is_ex_image(false);
			set_image( desc->get_image_id( ribi, snow ) );
			set_foreground_image( desc->get_image_id( ribi, snow, true ) );
			break;
		case image_slope:
			set_is_ex_image(false);
			set_image( desc->get_slope_image_id( (slope_t::type)ribi, snow ) );
			set_foreground_image( desc->get_slope_image_id( (slope_t::type)ribi, snow, true ) );
			break;
		case image_switch:
			set_is_ex_image(false);
			set_image( desc->get_switch_image_id(ribi, snow, switch_nw) );
			set_foreground_image( desc->get_switch_image_id(ribi, snow, switch_nw, true) );
			break;
		case image_ex:
			set_is_ex_image(true);
			set_image( desc->get_switch_ex_image_id(ribi, snow, switch_nw) );
			set_foreground_image( desc->get_switch_ex_image_id(ribi, snow, switch_nw, true) );
			break;
		case image_diagonal:
			set_is_ex_image(false);
			set_image( desc->get_diagonal_image_id(ribi, snow) );
			set_foreground_image( desc->get_diagonal_image_id(ribi, snow, true) );
			break;
	}
}


// much faster recalculation of season image
bool weg_t::check_season(const bool calc_only_season_change)
{
	if(  calc_only_season_change  ) { // nothing depends on season, only snowline
		return true;
	}

	// no way to calculate this or no image set (not visible, in tunnel mouth, etc)
	if(  desc == NULL  ||  image == IMG_EMPTY  ) {
		return true;
	}

	grund_t *from = welt->lookup( get_pos() );
	if(  from->ist_bruecke()  &&  from->obj_bei(0) == this  ) {
		// first way on a bridge (bruecke_t will set the image)
		return true;
	}

	// use snow image if above snowline and above ground
	bool snow = (from->ist_karten_boden()  ||  !from->ist_tunnel())  &&  (get_pos().z  + from->get_weg_yoff()/TILE_HEIGHT_STEP >= welt->get_snowline()  ||  welt->get_climate( get_pos().get_2d() ) == arctic_climate);
	bool old_snow = (flags&IS_SNOW) != 0;
	if(  !(snow ^ old_snow)  ) {
		// season is not changing ...
		return true;
	}

	// set snow flake
	flags &= ~IS_SNOW;
	if(  snow  ) {
		flags |= IS_SNOW;
	}

	slope_t::type hang = from->get_weg_hang();
	if(  hang != slope_t::flat  ) {
		set_images( image_slope, hang, snow );
		return true;
	}

	if(  is_diagonal()  ) {
		set_images( image_diagonal, ribi, snow );
	}
	else if(  ribi_t::is_threeway( ribi )  &&  desc->has_switch_image()  ) {
		// there might be two states of the switch; remember it when changing seasons
		if(  image == desc->get_switch_image_id( ribi, old_snow, false )  ) {
			set_images( image_switch, ribi, snow, false );
		}
		else if(  image == desc->get_switch_image_id( ribi, old_snow, true )  ) {
			set_images( image_switch, ribi, snow, true );
		}
		else {
			set_images( image_flat, ribi, snow );
		}
	}
	else {
		set_images( image_flat, ribi, snow );
	}

	return true;
}


#ifdef MULTI_THREAD
void weg_t::lock_mutex()
{
	pthread_mutex_lock( &weg_calc_image_mutex );
}


void weg_t::unlock_mutex()
{
	pthread_mutex_unlock( &weg_calc_image_mutex );
}
#endif


void weg_t::calc_image()
{
#ifdef MULTI_THREAD
	pthread_mutex_lock( &weg_calc_image_mutex );
#endif
	grund_t *from = welt->lookup(get_pos());
	grund_t *to;
	image_id old_image = image;

	if(  from==NULL  ||  desc==NULL  ) {
		// no ground, in tunnel
		set_image(IMG_EMPTY);
		set_foreground_image(IMG_EMPTY);
		if(  from==NULL  ) {
			dbg->error( "weg_t::calc_image()", "Own way at %s not found!", get_pos().get_str() );
		}
#ifdef MULTI_THREAD
		pthread_mutex_unlock( &weg_calc_image_mutex );
#endif
		return; // otherwise crashing during enlargement
	}
	else if(  from->ist_tunnel() &&  from->ist_karten_boden()  &&  (grund_t::underground_mode==grund_t::ugm_none || (grund_t::underground_mode==grund_t::ugm_level && from->get_hoehe()<grund_t::underground_level))  ) {
		// handled by tunnel mouth, no underground mode
//		set_image(IMG_EMPTY);
//		set_foreground_image(IMG_EMPTY);
	}
	else if(  from->ist_bruecke()  &&  from->obj_bei(0)==this  ) {
		// first way on a bridge (bruecke_t will set the image)
#ifdef MULTI_THREAD
		pthread_mutex_unlock( &weg_calc_image_mutex );
#endif
		return;
	}
	else {
		// use snow image if above snowline and above ground
		bool snow = (from->ist_karten_boden()  ||  !from->ist_tunnel())  &&  (get_pos().z + from->get_weg_yoff()/TILE_HEIGHT_STEP >= welt->get_snowline() || welt->get_climate( get_pos().get_2d() ) == arctic_climate  );
		flags &= ~IS_SNOW;
		if(  snow  ) {
			flags |= IS_SNOW;
		}

		slope_t::type hang = from->get_weg_hang();
		if(hang != slope_t::flat) {
			// on slope
			set_images(image_slope, hang, snow);
		}
		else {
			static int recursion = 0; /* Communicate among different instances of this method */

			// flat way
			set_images(image_flat, ribi, snow);

			// recalc image of neighbors also when this changed to non-diagonal
			if(recursion == 0) {
				recursion++;
				for(int r = 0; r < 4; r++) {
					if(  from->get_neighbour(to, get_waytype(), ribi_t::nesw[r])  ) {
						// can fail on water tiles
						if(  weg_t *w=to->get_weg(get_waytype())  )  {
							// and will only change the outcome, if it has a diagonal image ...
							if(  w->get_desc()->has_diagonal_image()  ) {
								w->calc_image();
							}
						}
					}
				}
				recursion--;
			}

			// try diagonal image
			if(  desc->has_diagonal_image()  ) {
				check_diagonal();

				// now apply diagonal image
				if(is_diagonal()) {
					if( desc->get_diagonal_image_id(ribi, snow) != IMG_EMPTY  ||
					    desc->get_diagonal_image_id(ribi, snow, true) != IMG_EMPTY) {
						set_images(image_diagonal, ribi, snow);
					}
				}
			}

#if COLOUR_DEPTH != 0 && MULTI_THREAD != 0
			if(!is_diagonal() && desc->has_switch_image()){
				waytype_t type_name = get_waytype();
				if(type_name == track_wt){
					select_switch_image(snow);
				}
				else if(type_name == road_wt){
					select_switch_road_image(snow);
				}
			}
#endif

		}
	}
	if(  image!=old_image  ) {
		mark_image_dirty(old_image, from->get_weg_yoff());
		mark_image_dirty(image, from->get_weg_yoff());
	}
#ifdef MULTI_THREAD
	pthread_mutex_unlock( &weg_calc_image_mutex );
#endif
}

void weg_t::select_switch_image(bool snow){
	if(ribi == ribi_t::northsoutheast){
		grund_t *from = welt->lookup(get_pos());
		grund_t *east;
		if(  from->get_neighbour(east, track_wt, ribi_t::east)  ) {
			ribi_t::ribi east_ribi = east->get_weg(track_wt)->get_ribi_unmasked();
			if(east_ribi == ribi_t::northwest){
				set_images(image_switch, ribi, snow, true);
				return;
			}
			else if(east_ribi == ribi_t::southwest){
				set_images(image_switch, ribi, snow, false);
				return;
			}
			else if(east_ribi == ribi_t::northsouthwest ){
				grund_t *north;
				grund_t *south;
				grund_t *north_east;
				grund_t *south_east;
				if(  from->get_neighbour(north, track_wt, ribi_t::north) && from->get_neighbour(south, track_wt, ribi_t::south)
						&& east->get_neighbour(north_east, track_wt, ribi_t::north) && east->get_neighbour(south_east, track_wt, ribi_t::south))
				{
					ribi_t::ribi north_masked_ribi = north->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi south_masked_ribi = south->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi north_east_masked_ribi = north_east->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi south_east_masked_ribi = south_east->get_weg(track_wt)->get_ribi_masked();
					if( ((north_masked_ribi & ribi_t::northwest) && (south_east_masked_ribi & ribi_t::southeast) && (south_masked_ribi != ribi_t::north || north_east_masked_ribi != ribi_t::south)) 
						|| ((north_masked_ribi & ribi_t::southwest) && (south_east_masked_ribi & ribi_t::northeast) && (south_masked_ribi != ribi_t::south || north_east_masked_ribi != ribi_t::north)) )
					{
						set_images(image_switch, ribi, snow, true);
						return;
					}
					else if( ((south_masked_ribi & ribi_t::southwest) && (north_east_masked_ribi & ribi_t::northeast) && (north_masked_ribi != ribi_t::south || south_east_masked_ribi != ribi_t::north))
						|| ((south_masked_ribi & ribi_t::northwest) && (north_east_masked_ribi & ribi_t::southeast) && (north_masked_ribi != ribi_t::north || south_east_masked_ribi != ribi_t::south)) )
					{
						set_images(image_switch, ribi, snow, false);
						return;
					}
				}
			}
			else if(desc->has_switch_ex_image() && from->is_use_track_wt_ex_image(ribi_t::backward(ribi))){
				set_images(image_ex, ribi, snow, false);
				return;
			}
		}
	}
	else if(ribi == ribi_t::northsouthwest){
		grund_t *from = welt->lookup(get_pos());
		grund_t *west;
		if(  from->get_neighbour(west, track_wt, ribi_t::west)  ){
			ribi_t::ribi west_ribi = west->get_weg(track_wt)->get_ribi_unmasked();
			if(west_ribi == ribi_t::southeast){
				set_images(image_switch, ribi, snow, true);
				return;
			}
			else if(west_ribi == ribi_t::northeast){
				set_images(image_switch, ribi, snow, false);
				return;
			}
			else if(west_ribi == ribi_t::northsoutheast){
				grund_t *north;
				grund_t *south;
				grund_t *north_west;
				grund_t *south_west;
				if(  from->get_neighbour(north, track_wt, ribi_t::north) && from->get_neighbour(south, track_wt, ribi_t::south)
						&& west->get_neighbour(north_west, track_wt, ribi_t::north) && west->get_neighbour(south_west, track_wt, ribi_t::south))
				{
					ribi_t::ribi north_masked_ribi = north->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi south_masked_ribi = south->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi north_west_masked_ribi = north_west->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi south_west_masked_ribi = south_west->get_weg(track_wt)->get_ribi_masked();
					if( ((south_masked_ribi & ribi_t::northeast) && (north_west_masked_ribi & ribi_t::southwest) && (north_masked_ribi != ribi_t::north || south_west_masked_ribi != ribi_t::south))
						|| ((south_masked_ribi & ribi_t::southeast) && (north_west_masked_ribi & ribi_t::northwest) && (north_masked_ribi != ribi_t::south || south_west_masked_ribi != ribi_t::north)) )
					{
						set_images(image_switch, ribi, snow, true);
						return;
					}
					else if( ((north_masked_ribi & ribi_t::northeast) && (south_west_masked_ribi & ribi_t::southwest) && (south_masked_ribi != ribi_t::north || north_west_masked_ribi != ribi_t::south))
						|| ((north_masked_ribi & ribi_t::southeast) && (south_west_masked_ribi & ribi_t::northwest) && (south_masked_ribi != ribi_t::south || north_west_masked_ribi != ribi_t::north)) )
					{
						set_images(image_switch, ribi, snow, false);
						return;
					}
				}
			}
			else if(desc->has_switch_ex_image() && from->is_use_track_wt_ex_image(ribi_t::backward(ribi))){
				set_images(image_ex, ribi, snow, false);
				return;
			}
		}
	}
	else if(ribi == ribi_t::northeastwest){
		grund_t *from = welt->lookup(get_pos());
		grund_t *north;
		if(  from->get_neighbour(north, track_wt, ribi_t::north)  ){
			ribi_t::ribi north_ribi = north->get_weg(track_wt)->get_ribi_unmasked();
			if(north_ribi == ribi_t::southeast){
				set_images(image_switch, ribi, snow, true);
				return;
			}
			else if(north_ribi == ribi_t::southwest){
				set_images(image_switch, ribi, snow, false);
				return;
			}
			else if(north_ribi == ribi_t::southeastwest){
				grund_t *west;
				grund_t *east;
				grund_t *north_west;
				grund_t *north_east;
				if(  from->get_neighbour(west, track_wt, ribi_t::west) && from->get_neighbour(east, track_wt, ribi_t::east)
						&& north->get_neighbour(north_west, track_wt, ribi_t::west) && north->get_neighbour(north_east, track_wt, ribi_t::east))
				{
					ribi_t::ribi west_masked_ribi = west->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi east_masked_ribi = east->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi north_west_masked_ribi = north_west->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi north_east_masked_ribi = north_east->get_weg(track_wt)->get_ribi_masked();
					if( ((east_masked_ribi & ribi_t::southwest) && (north_west_masked_ribi & ribi_t::northeast) && (west_masked_ribi != ribi_t::west || north_east_masked_ribi != ribi_t::east))
						|| ((east_masked_ribi & ribi_t::southeast) && (north_west_masked_ribi & ribi_t::northwest) && (west_masked_ribi != ribi_t::east || north_east_masked_ribi != ribi_t::west)) )
					{
						set_images(image_switch, ribi, snow, true);
						return;
					}
					else if( ((west_masked_ribi & ribi_t::southwest) && (north_east_masked_ribi & ribi_t::northeast) && (east_masked_ribi != ribi_t::west || north_west_masked_ribi != ribi_t::east))
						|| ((west_masked_ribi & ribi_t::southeast) && (north_east_masked_ribi & ribi_t::northwest) && (east_masked_ribi != ribi_t::east || north_west_masked_ribi != ribi_t::west)) )
					{
						set_images(image_switch, ribi, snow, false);
						return;
					}
				}
			}
			else if(desc->has_switch_ex_image() && from->is_use_track_wt_ex_image(ribi_t::backward(ribi))){
				set_images(image_ex, ribi, snow, false);
				return;
			}
		}
	}
	else if(ribi == ribi_t::southeastwest){
		grund_t *from = welt->lookup(get_pos());
		grund_t *south;
		if(  from->get_neighbour(south, track_wt, ribi_t::south) ){
			ribi_t::ribi south_ribi = south->get_weg(track_wt)->get_ribi_unmasked();
			if(south_ribi == ribi_t::northwest){
				set_images(image_switch, ribi, snow, true);
				return;
			}
			else if(south_ribi == ribi_t::northeast){
				set_images(image_switch, ribi, snow, false);
				return;
			}
			else if(south_ribi == ribi_t::northeastwest){
				grund_t *west;
				grund_t *east;
				grund_t *south_west;
				grund_t *south_east;
				if(  from->get_neighbour(west, track_wt, ribi_t::west) && from->get_neighbour(east, track_wt, ribi_t::east)
						&& south->get_neighbour(south_west, track_wt, ribi_t::west) && south->get_neighbour(south_east, track_wt, ribi_t::east))
				{
					ribi_t::ribi west_masked_ribi = west->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi east_masked_ribi = east->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi south_west_masked_ribi = south_west->get_weg(track_wt)->get_ribi_masked();
					ribi_t::ribi south_east_masked_ribi = south_east->get_weg(track_wt)->get_ribi_masked();
					if( ((west_masked_ribi & ribi_t::northwest) && (south_east_masked_ribi & ribi_t::southeast) && (east_masked_ribi != ribi_t::west || south_west_masked_ribi != ribi_t::east))
						|| ((west_masked_ribi & ribi_t::northeast) && (south_east_masked_ribi & ribi_t::southwest) && (east_masked_ribi != ribi_t::east || south_west_masked_ribi != ribi_t::west)) )
					{
						set_images(image_switch, ribi, snow, true);
						return;
					}
					else if( ((east_masked_ribi & ribi_t::northwest) && (south_west_masked_ribi & ribi_t::southeast) && (west_masked_ribi != ribi_t::west || south_east_masked_ribi != ribi_t::east))
						|| ((east_masked_ribi & ribi_t::northeast) && (south_west_masked_ribi & ribi_t::southwest) && (west_masked_ribi != ribi_t::east || south_east_masked_ribi != ribi_t::west)) )
					{
						set_images(image_switch, ribi, snow, false);
						return;
					}
				}
			}
			else if(desc->has_switch_ex_image() && from->is_use_track_wt_ex_image(ribi_t::backward(ribi))){
				set_images(image_ex, ribi, snow, false);
				return;
			}
		}
	}
	else if(ribi == ribi_t::all && desc->has_switch_ex_image()){
		grund_t *from = welt->lookup(get_pos());
		grund_t *north;
		grund_t *south;
		grund_t *west;
		grund_t *east;
		if(  from->get_neighbour(north, track_wt, ribi_t::north) && from->get_neighbour(south, track_wt, ribi_t::south)
		&& from->get_neighbour(west, track_wt, ribi_t::west) && from->get_neighbour(east, track_wt, ribi_t::east))
		{
			ribi_t::ribi north_ribi = north->get_weg(track_wt)->get_ribi_unmasked();
			ribi_t::ribi south_ribi = south->get_weg(track_wt)->get_ribi_unmasked();
			ribi_t::ribi west_ribi = west->get_weg(track_wt)->get_ribi_unmasked();
			ribi_t::ribi east_ribi = east->get_weg(track_wt)->get_ribi_unmasked();
			if(ribi_t::is_bend(north_ribi) && ribi_t::is_bend(south_ribi) && (north_ribi & south_ribi)){
				set_images(image_ex, (ribi_t::backward(north_ribi) | ribi_t::backward(south_ribi)), snow, true);
				return;
			}
			else if(ribi_t::is_bend(east_ribi) && ribi_t::is_bend(west_ribi) && (east_ribi & west_ribi)){
				set_images(image_ex, (ribi_t::backward(east_ribi) | ribi_t::backward(west_ribi)), snow, true);
				return;
			}
			ribi_t::ribi north_masked_ribi = north->get_weg(track_wt)->get_ribi_masked();
			// ribi_t::ribi south_masked_ribi = south->get_weg(get_waytype())->get_ribi_masked();
			
			if( east_ribi == ribi_t::northwest && east->get_weg(track_wt)->get_ribi() == ribi_t::west){
				grund_t *southwest;
				if( west->get_neighbour(southwest, track_wt, ribi_t::south) ){
					ribi_t::ribi southwest_masked_ribi = southwest->get_weg(track_wt)->get_ribi_masked();
					if((north_masked_ribi & ribi_t::north) && (southwest_masked_ribi & ribi_t::southwest)){
						set_images(image_ex, ribi_t::southeastwest, snow, false);
						west->get_weg(track_wt)->set_images(image_switch, west_ribi, snow, false);
						return;
					}
				}
			}
/* 			else if( east_ribi == ribi_t::southwest ){
				grund_t *northwest;
				if( west->get_neighbour(northwest, get_waytype(), ribi_t::north) && northwest->get_weg(get_waytype())){
					ribi_t::ribi northwest_masked_ribi = northwest->get_weg(get_waytype())->get_ribi_masked();
					if((south_masked_ribi & ribi_t::south) && (northwest_masked_ribi & ribi_t::north)){
						set_images(image_ex, ribi_t::northeastwest, snow, false);
						west->get_weg(get_waytype())->set_images(image_switch, west_ribi, snow, true);
						return;
					}
				}
			} */
		}
	}
}


void weg_t::select_switch_road_image(bool snow){
	if(ribi == ribi_t::northsoutheast){
		grund_t *from = welt->lookup(get_pos());
		grund_t *east;
		if( from->get_neighbour(east, road_wt, ribi_t::east) && ribi_t::is_threeway(east->get_weg(road_wt)->get_ribi_unmasked())){
			grund_t *north;
			grund_t *south;
			if( from->get_neighbour(north, road_wt, ribi_t::north) && from->get_neighbour(south, road_wt, ribi_t::south) ){
				ribi_t::ribi north_ribi = north->get_weg(road_wt)->get_ribi_unmasked();
				ribi_t::ribi south_ribi = south->get_weg(road_wt)->get_ribi_unmasked();
				if( !ribi_t::is_threeway(north_ribi) && !ribi_t::is_threeway(south_ribi) ){
					set_images(image_switch, ribi, snow, false);
					return;
				}
				else if(desc->has_switch_ex_image()){
					if( !ribi_t::is_threeway(south_ribi) ){
						set_images(image_ex, ribi, snow, true);
						return;
					}
					else if( !ribi_t::is_threeway(north_ribi) ){
						set_images(image_ex, ribi, snow, false);
						return;						
					}
				}
			}
		}
	}
	else if(ribi == ribi_t::northsouthwest){
		grund_t *from = welt->lookup(get_pos());
		grund_t *west;
		if( from->get_neighbour(west, road_wt, ribi_t::west) && ribi_t::is_threeway(west->get_weg(road_wt)->get_ribi_unmasked())){
			grund_t *north;
			grund_t *south;
			if( from->get_neighbour(north, road_wt, ribi_t::north) && from->get_neighbour(south, road_wt, ribi_t::south) ){
				ribi_t::ribi north_ribi = north->get_weg(road_wt)->get_ribi_unmasked();
				ribi_t::ribi south_ribi = south->get_weg(road_wt)->get_ribi_unmasked();
				if( !ribi_t::is_threeway(north_ribi) && !ribi_t::is_threeway(south_ribi) ){
					set_images(image_switch, ribi, snow, false);
					return;
				}
				else if(desc->has_switch_ex_image()){
					if( !ribi_t::is_threeway(north_ribi) ){
						set_images(image_ex, ribi, snow, true);
						return;
					}
					else if( !ribi_t::is_threeway(south_ribi) ){
						set_images(image_ex, ribi, snow, false);
						return;						
					}
				}
			}
		}
	}
	else if(ribi == ribi_t::northeastwest){
		grund_t *from = welt->lookup(get_pos());
		grund_t *north;
		if( from->get_neighbour(north, road_wt, ribi_t::north) && ribi_t::is_threeway(north->get_weg(road_wt)->get_ribi_unmasked())){
			grund_t *east;
			grund_t *west;
			if( from->get_neighbour(east, road_wt, ribi_t::east) && from->get_neighbour(west, road_wt, ribi_t::west) ){
				ribi_t::ribi east_ribi = east->get_weg(road_wt)->get_ribi_unmasked();
				ribi_t::ribi west_ribi = west->get_weg(road_wt)->get_ribi_unmasked();
				if( !ribi_t::is_threeway(east_ribi) && !ribi_t::is_threeway(west_ribi) ){
					set_images(image_switch, ribi, snow, false);
					return;
				}
				else if(desc->has_switch_ex_image()){
					if( !ribi_t::is_threeway(west_ribi) ){
						set_images(image_ex, ribi, snow, true);
						return;
					}
					else if( !ribi_t::is_threeway(east_ribi) ){
						set_images(image_ex, ribi, snow, false);
						return;						
					}
				}
			}
		}
	}
	else if(ribi == ribi_t::southeastwest){
		grund_t *from = welt->lookup(get_pos());
		grund_t *south;
		grund_t *east;
		grund_t *west;		
		if(  from->get_neighbour(south, road_wt, ribi_t::south) && from->get_neighbour(east, road_wt, ribi_t::east) && from->get_neighbour(west, road_wt, ribi_t::west)  ){
			ribi_t::ribi south_ribi = south->get_weg(road_wt)->get_ribi_unmasked();
			ribi_t::ribi east_ribi = east->get_weg(road_wt)->get_ribi_unmasked();
			ribi_t::ribi west_ribi = west->get_weg(road_wt)->get_ribi_unmasked();			
			if(south_ribi == ribi_t::all || ribi_t::is_threeway(south_ribi)){
				if((ribi_t::is_twoway(east_ribi) || ribi_t::is_single(east_ribi)) && (ribi_t::is_twoway(west_ribi) || ribi_t::is_single(west_ribi))){
					set_images(image_switch, ribi, snow, false);
					return;
				}
				else if(desc->has_switch_ex_image()){
					if(ribi_t::is_twoway(east_ribi) || ribi_t::is_single(east_ribi)){
						set_images(image_ex, ribi, snow, true);
						return;
					}
					else if(ribi_t::is_twoway(west_ribi) || ribi_t::is_single(west_ribi)){
						set_images(image_ex, ribi, snow, false);
						return;						
					}
				}					
			}
		}
	}
	else if(ribi == ribi_t::all){
		grund_t *from = welt->lookup(get_pos());
		grund_t *north;
		grund_t *south;
		grund_t *west;
		grund_t *east;
		if(  from->get_neighbour(north, road_wt, ribi_t::north) && from->get_neighbour(south, road_wt, ribi_t::south)
					&& from->get_neighbour(west, road_wt, ribi_t::west) && from->get_neighbour(east, road_wt, ribi_t::east)){
			ribi_t::ribi north_ribi = north->get_weg(road_wt)->get_ribi_unmasked();
			ribi_t::ribi south_ribi = south->get_weg(road_wt)->get_ribi_unmasked();
			ribi_t::ribi west_ribi = west->get_weg(road_wt)->get_ribi_unmasked();
			ribi_t::ribi east_ribi = east->get_weg(road_wt)->get_ribi_unmasked();
			if(desc->has_switch_ex_image()){
				if( ribi_t::is_threeway(east_ribi) ){
					if( ribi_t::is_threeway(south_ribi) && !ribi_t::is_threeway(north_ribi) ){
						set_images(image_switch, ribi, snow, true);
						return;
					}
					else if( ribi_t::is_threeway(north_ribi) && !ribi_t::is_threeway(south_ribi) ){
						set_images(image_switch, ribi, snow, false);
						return;
					}
					else{
						set_images(image_switch, ribi_t::northsoutheast, snow, true);
						return;
					}
				}
				else if ( ribi_t::is_threeway(west_ribi) ){
					if( ribi_t::is_threeway(north_ribi) && !ribi_t::is_threeway(south_ribi) ){
						set_images(image_ex, ribi, snow, true);
						return;
					}
					else if( ribi_t::is_threeway(south_ribi) && !ribi_t::is_threeway(north_ribi) ){
						set_images(image_ex, ribi, snow, false);
						return;
					}
					else{
						set_images(image_switch, ribi_t::northsouthwest, snow, true);
						return;
					}
				}
				else if( ribi_t::is_threeway(north_ribi) && !ribi_t::is_threeway(south_ribi) ){
					set_images(image_switch, ribi_t::northeastwest, snow, true);
					return;
				}
				else if( ribi_t::is_threeway(south_ribi) && !ribi_t::is_threeway(north_ribi) ){
					set_images(image_switch, ribi_t::southeastwest, snow, true);
					return;
				}
			}
			else{
				if( ribi_t::is_threeway(east_ribi) ){
					set_images(image_switch, ribi_t::northsoutheast, snow, true);
					return;
				}
				else if( ribi_t::is_threeway(west_ribi) ){
					set_images(image_switch, ribi_t::northsouthwest, snow, true);
					return;
				}
				else if( ribi_t::is_threeway(north_ribi) ){
					set_images(image_switch, ribi_t::northeastwest, snow, true);
					return;
				}
				else if( ribi_t::is_threeway(south_ribi) ){
					set_images(image_switch, ribi_t::southeastwest, snow, true);
					return;
				}
			}
		}
	}
	return;
}

// checks, if this way qualifies as diagonal
void weg_t::check_diagonal()
{
	bool diagonal = false;
	flags &= ~IS_DIAGONAL;

	const ribi_t::ribi ribi = get_ribi_unmasked();
	if(  !ribi_t::is_bend(ribi)  ) {
		// This is not a curve, it can't be a diagonal
		return;
	}

	grund_t *from = welt->lookup(get_pos());
	grund_t *to;

	ribi_t::ribi r1 = ribi_t::none;
	ribi_t::ribi r2 = ribi_t::none;

	// get the ribis of the ways that connect to us
	// r1 will be 45 degree clockwise ribi (eg northeast->east), r2 will be anticlockwise ribi (eg northeast->north)
	if(  from->get_neighbour(to, get_waytype(), ribi_t::rotate45(ribi))  ) {
		r1 = to->get_weg_ribi_unmasked(get_waytype());
	}

	if(  from->get_neighbour(to, get_waytype(), ribi_t::rotate45l(ribi))  ) {
		r2 = to->get_weg_ribi_unmasked(get_waytype());
	}

	// diagonal if r1 or r2 are our reverse and neither one is 90 degree rotation of us
	diagonal = (r1 == ribi_t::backward(ribi) || r2 == ribi_t::backward(ribi)) && r1 != ribi_t::rotate90l(ribi) && r2 != ribi_t::rotate90(ribi);

	if(  diagonal  ) {
		flags |= IS_DIAGONAL;
	}
}


/**
 * new month
 */
void weg_t::new_month()
{
	for (int type=0; type<MAX_WAY_STATISTICS; type++) {
		for (int month=MAX_WAY_STAT_MONTHS-1; month>0; month--) {
			statistics[month][type] = statistics[month-1][type];
		}
		statistics[0][type] = 0;
	}
}


// correct speed and maintenance
void weg_t::finish_rd()
{
	player_t *player = get_owner();
	if(  player  &&  desc  ) {
		player_t::add_maintenance( player,  desc->get_maintenance(), desc->get_finance_waytype() );
	}
}


// returns NULL, if removal is allowed
// players can remove public owned ways
const char *weg_t::is_deletable(const player_t *player)
{
	if(  get_owner_nr()==PUBLIC_PLAYER_NR  ) {
		return NULL;
	}
	return obj_t::is_deletable(player);
}
