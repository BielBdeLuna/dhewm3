//this code is open source and Apache License version 2.0 derivated heavily 
//from thwe code by Abraham T. Stolk @ https://github.com/stolk/GPGOAP

#ifndef __PLANNER_H__
#define __PLANNER_H__

#define MAXATOMS 64
#define MAXACTIONS 64

typedef long long int bfield_t;

//!< Describes the world state by listing values (t/f) for all known atoms.
typedef struct {
	bfield_t values;	//!< Values for atoms.
	bfield_t dontcare;	//!< Mask for atoms that do not matter.
} worldstate_t;

//!< Action planner that keeps track of world state atoms and its action repertoire.
typedef struct {
	const char *atm_names[ MAXATOMS ];	//!< Names associated with all world state atoms.
	int numatoms;				//!< Number of world state atoms.

	const char *act_names[ MAXACTIONS ];	//!< Names of all actions in repertoire.
	worldstate_t act_pre[ MAXACTIONS ];	//!< Preconditions for all actions.
	worldstate_t act_pst[ MAXACTIONS ];	//!< Postconditions for all actions (action effects).
	int act_costs[ MAXACTIONS ];		//!< Cost for all actions.
	int numactions;				//!< The number of actions in out repertoire.

} actionplanner_t;

class blPlanner {

public:

    blPlanner();
    ~blPlaner();

    void    planner_clear( actionplanner_t* ap );
    void    worldstate_clear( worldstate_t* ws );
    bool    worldstate_set( actionplanner_t* ap, worldstate_t* ws, const char* atomname, bool value );
    bool    set_preCond( actionplanner_t* ap, const char* actionname, const char* atomname, bool value );
    bool    set_postCond( actionplanner_t* ap, const char* actionname, const char* atomname, bool value );
    bool    set_cost( actionplanner_t* ap, const char* actionname, int cost );
    void    description( actionplanner_t* ap, char* buf, int sz );
    void    worldstate_description( const actionplanner_t* ap, const worldstate_t* ws, char* buf, int sz );
    int     get_possible_state_transitions( actionplanner_t* ap, worldstate_t fr, worldstate_t* to, const char** actionnames, int* actioncosts, int cnt );

}

#endif /* !__PLANNER_H__ */

