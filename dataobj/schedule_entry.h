#ifndef schedule_entry_t_h
#define schedule_entry_t_h

#include "koord3d.h"
#include "../linehandle_t.h"

/**
 * A schedule entry.
 * @author Hj. Malthaner
 */
struct schedule_entry_t
{
public:
	schedule_entry_t() {}

	schedule_entry_t(koord3d const& pos, uint const minimum_loading, sint8 const waiting_time_shift) :
		pos(pos),
		minimum_loading(minimum_loading),
		waiting_time_shift(waiting_time_shift),
		couple_line(linehandle_t()),
		line_wait_for(linehandle_t()),
		uncouple_line(linehandle_t())
	{}

	/**
	 * target position
	 * @author Hj. Malthaner
	 */
	koord3d pos;

	/**
	 * Wait for % load at this stops
	 * (ignored on waypoints)
	 * @author Hj. Malthaner
	 */
	uint8 minimum_loading;

	/**
	 * maximum waiting time in 1/2^(16-n) parts of a month
	 * (only active if minimum_loading!=0)
	 * @author prissi
	 */
	sint8 waiting_time_shift;
	
	/**
	 * entries related to coupling and decoupling
	 * couple_line: the line of another convoy which the convoy goes with
	 * line_wait_for: the line which the convoy wait to couple with
	 * uncouple_line: target line to uncouple from the convoy
	 * @author THLeaderH
	 */
	linehandle_t couple_line;
	linehandle_t line_wait_for;
	linehandle_t uncouple_line;
};

inline bool operator ==(const schedule_entry_t &a, const schedule_entry_t &b)
{
	return a.pos == b.pos  &&  a.minimum_loading == b.minimum_loading  &&  a.waiting_time_shift == b.waiting_time_shift;
}


#endif
