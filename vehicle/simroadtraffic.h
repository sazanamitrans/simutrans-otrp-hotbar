/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_SIMROADTRAFFIC_H
#define VEHICLE_SIMROADTRAFFIC_H


#include "simvehicle.h"
#include "overtaker.h"

#include "../tpl/stringhashtable_tpl.h"
#include "../ifc/sync_steppable.h"

class citycar_desc_t;
class karte_t;

/**
 * Base class for traffic participants with random movement
 *
 * Transport vehicles are defined in simvehicle.h, because they greatly
 * differ from the vehicles defined herein for the individual traffic
 * (pedestrians, citycars, movingobj aka flock of sheep).
 */
class road_user_t : public vehicle_base_t, public sync_steppable
{
protected:
	/**
	 * Distance count
	 */
	uint32 weg_next;

	/* ms until destruction
	 */
	sint32 time_to_life;

protected:
	waytype_t get_waytype() const OVERRIDE { return road_wt; }

	road_user_t();

	/**
	 * Creates thing at position given by @p gr.
	 * Does not add it to the tile!
	 * @param random number to compute initial direction.
	 */
	road_user_t(grund_t* gr, uint16 random);

public:
	virtual ~road_user_t();

	const char *get_name() const OVERRIDE = 0;
	typ get_typ() const OVERRIDE  = 0;

	/**
	 * Open a new observation window for the object.
	 */
	void show_info() OVERRIDE;

	void rdwr(loadsave_t *file) OVERRIDE;

	// finalizes direction
	void finish_rd() OVERRIDE;

	// we allow to remove all cars etc.
	const char *is_deletable(const player_t *) OVERRIDE { return NULL; }
};


class private_car_t : public road_user_t, public overtaker_t
{
private:
	static stringhashtable_tpl<const citycar_desc_t *> table;

	const citycar_desc_t *desc;

	// time to life in blocks
#ifdef DESTINATION_CITYCARS
	koord target;
#endif
	
	vector_tpl<koord3d> route;
	uint8 route_index;

	/**
	 * Actual speed
	 */
	uint16 current_speed;

	uint32 ms_traffic_jam;
	
	// 0: not fixed, -1: fixed to traffic lane, 1: fixed to passing lane
	sint8 lane_affinity;
	uint8 lane_affinity_end_index;
	
	bool next_cross_lane;
	uint32 request_cross_ticks;
	
	/**
	 * the route index of the point to quit yielding lane
	 * == -1 means this convoi isn't yielding.
	 * @author teamhimeH
	 */
	sint8 yielding_quit_index;
	bool requested_change_lane;
	
	vector_tpl<koord3d> reserving_tiles;
	
	koord3d pos_prev;

	grund_t* hop_check() OVERRIDE;

	void calc_disp_lane();
	
	bool calc_lane_affinity(uint8 lane_affinity_sign); // If true, lane fixing started.
	
	uint8 idx_in_scope(uint8 org, sint8 offset) const;
	
	bool is_rerouting_needed() const;

protected:
	void rdwr(loadsave_t *file) OVERRIDE;

	void calc_image() OVERRIDE;
	
	koord3d find_destination(uint8 target_index);

public:
	private_car_t(loadsave_t *file);

	/**
	 * Creates citycar at position given by @p gr.
	 * Does not add car to the tile!
	 * If @p name == NULL then a random car is created.
	 */
	private_car_t(grund_t* gr, koord target, const char* name = NULL);

	virtual ~private_car_t();

	void rotate90() OVERRIDE;

	const citycar_desc_t *get_desc() const { return desc; }

	sync_result sync_step(uint32 delta_t) OVERRIDE;

	void hop(grund_t *gr) OVERRIDE;
	bool ist_weg_frei(grund_t *gr);

	void enter_tile(grund_t* gr) OVERRIDE;
	void leave_tile() OVERRIDE;

	void calc_current_speed(grund_t*, uint32);
	uint16 get_current_speed() const {return current_speed;}

	const char *get_name() const OVERRIDE {return "Verkehrsteilnehmer";}
	typ get_typ() const OVERRIDE { return road_user; }

	/**
	 * @param[out] buf a description string for the object
	 * e.g. for the observation window/dialog
	 * @see simwin
	 */
	void info(cbuffer_t & buf) const OVERRIDE;

	// true, if this vehicle did not moved for some time
	bool is_stuck() OVERRIDE { return current_speed==0;}

	/** this function builds the list of the allowed citycars
	 * it should be called every month and in the beginning of a new game
	 */
	static void build_timeline_list(karte_t *welt);
	static bool list_empty();

	static bool register_desc(const citycar_desc_t *desc);
	static bool successfully_loaded();

	// since we must consider overtaking, we use this for offset calculation
	void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width, bool prev_based ) const;
	virtual void get_screen_offset( int &xoff, int &yoff, const sint16 raster_width ) const OVERRIDE{ get_screen_offset(xoff,yoff,raster_width,false); }

	overtaker_t *get_overtaker() OVERRIDE { return this; }

	// Overtaking for city cars
	virtual bool can_overtake(overtaker_t *other_overtaker, sint32 other_speed, sint16 steps_other) OVERRIDE;

	virtual vehicle_base_t* other_lane_blocked(const bool only_search_top) const;
	vehicle_base_t* is_there_car(grund_t *gr) const; // This is a helper function of other_lane_blocked
	
	uint16 get_speed_limit() const;
	
	bool get_next_cross_lane();
	void set_next_cross_lane(bool);
	
	void unreserve_all_tiles();
	
	void yield_lane_space();
	sint32 get_yielding_quit_index() const { return yielding_quit_index; }
	void quit_yielding_lane() { yielding_quit_index = -1; }
	void set_requested_change_lane(bool b) { requested_change_lane = b; }

	virtual void refresh(sint8,sint8) OVERRIDE;
};

#endif
