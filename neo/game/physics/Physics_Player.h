/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __PHYSICS_PLAYER_H__
#define __PHYSICS_PLAYER_H__

#include "physics/Physics_Actor.h"

/*
===================================================================================

	Player physics

	Simulates the motion of a player through the environment. Input from the
	player is used to allow a certain degree of control over the motion.

===================================================================================
*/

// movementType
typedef enum {
	PM_NORMAL,				// normal physics
	PM_DEAD,				// no acceleration or turning, but free falling
	PM_SPECTATOR,			// flying without gravity but with collision detection
	PM_FREEZE,				// stuck in place without control
	PM_NOCLIP				// flying without collision detection nor gravity
} pmtype_t;

typedef enum {
	WATERLEVEL_NONE,
	WATERLEVEL_FEET,
	WATERLEVEL_WAIST,
	WATERLEVEL_HEAD
} waterLevel_t;

#define	MAXTOUCH					32

typedef struct playerPState_s {
	idVec3					origin;
	idVec3					velocity;
	idVec3					localOrigin;
	idVec3					pushVelocity;
	float					stepUp;
	int						movementType;
	int						movementFlags;
	int						movementTime;
} playerPState_t;

enum SkimmingPhase {
	noSkimming = 0,
	SkimmingStart,
	SkimmingMovement,
	SkimmingHit,
	SkimmingCancel,
	SkimmingEnd,
	NumSkimmingPhases,
};

// This enumreation defines the phases of the mantling movement
enum EMantlePhase
{
	notMantling	= 0,
	mantlingHanging,
	mantlingPulling,
	mantlingShiftHands,
	mantlingPushing,
	fixTheClipping,
	NumMantlePhases,
};

class idPhysics_Player : public idPhysics_Actor {

public:
	CLASS_PROTOTYPE( idPhysics_Player );

							idPhysics_Player( void );

	void					Save( idSaveGame *savefile ) const;
	void					Restore( idRestoreGame *savefile );

							// initialisation
	void					SetSpeed( const float newWalkSpeed, const float newCrouchSpeed );
	void					SetMaxStepHeight( const float newMaxStepHeight );
	float					GetMaxStepHeight( void ) const;
	void					SetMaxJumpHeight( const float newMaxJumpHeight );
	void					SetMovementType( const pmtype_t type );
	void					SetPlayerInput( const usercmd_t &cmd, const idAngles &newViewAngles );
	void					SetKnockBack( const int knockBackTime );
	void					SetDebugLevel( bool set );
							// feed back from last physics frame
	waterLevel_t			GetWaterLevel( void ) const;
	int						GetWaterType( void ) const;
	bool					HasJumped( void ) const;
	bool					HasSteppedUp( void ) const;
	float					GetStepUp( void ) const;
	bool					IsCrouching( void ) const;
	bool					OnLadder( void ) const;
	const idVec3 &			PlayerGetOrigin( void ) const;	// != GetOrigin

public:	// common physics interface
	bool					Evaluate( int timeStepMSec, int endTimeMSec );
	void					UpdateTime( int endTimeMSec );
	int						GetTime( void ) const;

	void					GetImpactInfo( const int id, const idVec3 &point, impactInfo_t *info ) const;
	void					ApplyImpulse( const int id, const idVec3 &point, const idVec3 &impulse );
	bool					IsAtRest( void ) const;
	int						GetRestStartTime( void ) const;

	void					SaveState( void );
	void					RestoreState( void );

	void					SetOrigin( const idVec3 &newOrigin, int id = -1 );
	void					SetAxis( const idMat3 &newAxis, int id = -1 );

	void					Translate( const idVec3 &translation, int id = -1 );
	void					Rotate( const idRotation &rotation, int id = -1 );

	void					SetLinearVelocity( const idVec3 &newLinearVelocity, int id = 0 );

	const idVec3 &			GetLinearVelocity( int id = 0 ) const;

	void					SetPushed( int deltaTime );
	const idVec3 &			GetPushedLinearVelocity( const int id = 0 ) const;
	void					ClearPushedVelocity( void );

	void					SetMaster( idEntity *master, const bool orientated = true );
	void					WriteToSnapshot( idBitMsgDelta &msg ) const;
	void					ReadFromSnapshot( const idBitMsgDelta &msg );

	// Checks to see if there is a mantleable target within reach
	// of the player's view. If so, starts the mantle...
	// If the player is already mantling, this does nothing.
	void					PerformMantle();

	// This method returns
	// true if the player is mantling, false otherwise
	bool 					IsMantling() const;

	// This returns the current mantling phase
	EMantlePhase 			GetMantlePhase() const;

	// Cancels any current mantle
	void 					CancelMantle();

	void					PerformDodge( bool dodge_right );

	bool					IsSkimming( idVec3 &skimDir_forward, idVec3 &skimDir_right );
	void					CancelSkim( void );

protected:

	/*!
	* The current mantling phase
	*/
	EMantlePhase m_mantlePhase;

	/*!
	* How long will the current phase of the mantle operation take?
	* Uses milliseconds and counts down to 0.
	*/
	float m_mantleTime;

	/**
	 * greebo: Set to TRUE if the next mantling can start. Set to FALSE at the
	 *         beginning of a mantle process - the jump button has to be released
	 *         again during a non-mantling phase to set this to TRUE again.
	 */
	bool m_mantleStartPossible;

	/*!
	* Points along the mantle path
	*/
	idVec3 m_mantlePullStartPos;
	idVec3 m_mantlePullEndPos;
	idVec3 m_mantlePushEndPos;

	/*!
	* Pointer to the entity being mantled.
	* This is undefined if m_mantlePhase == notMantling_DarkModMantlePhase
	*/
	idEntity* m_p_mantledEntity;

	/*!
	* ID number of the entity being mantled
	* This is 0 if m_mantlePhase == notMantling_DarkModMantlePhase
	*/
	int m_mantledEntityID;

	/*!
	* Tracks, in milliseconds, how long jump button has been held down
	* Counts upwards from 0.
	*/
	float m_jumpHeldDownTime;

	/*!
	*
	* Internal method to start the mantle operation
	*
	* @param[in] initialMantlePhase The mantle phase in which the mantle starts.
	* @param[in] eyePos The position of the player's eyes in the world
	* @param[in] startPos The position of the player's feet at the start of the mantle
	* @param[in] endPos The position of the player's feet at the end of the mantle
	*/
	void 					StartMantle ( EMantlePhase initialMantlePhase, idVec3 eyePos, idVec3 startPos, idVec3 endPos );

	/*!
	* Internal method which determines the maximum vertical
	* and horizontal distances for mantling
	*
	* @param[out] out_maxVerticalReachDistance The distance that the player can reach vertically, from their current origin
	* @param[out] out_maxHorizontalReachDistance The distance that the player can reach horizontally, from their current origin
	* @param[out] out_maxMantleTraceDistance The maximum distance that the traces should look in front of the player for a mantle target
	*/
	void 					GetCurrentMantlingReachDistances ( float& out_maxVerticalReachDistance,
															   float& out_maxHorizontalReachDistance,
															   float& out_maxMantleTraceDistance
										  	  	  	  	  	 );

	/*!
	* This method runs the trace to find a mantle target
	* It first attempts to raycast along the player's gaze
	* direction to determine a target. If it doesn't find one,
	* then it tries a collision test along a vertical plane
	* from the players feet to their height, out in the direction
	* the player is facing.
	*
	* @param[in] maxMantleTraceDistance The maximum distance from the player that should be used in the traces
	* @param[in] eyePos The position of the player's eyes, used for the beginning of the gaze trace
	* @param[in] forwardVec The vector gives the direction that the player is facing
	* @param[out] out_trace This trace structure will hold the result of whichever trace succeeds. If both fail, the trace fraction will be 1.0
	*/
	void 					MantleTargetTrace ( float maxZmaxMantleTraceDistance,
												const idVec3& eyePos,
												const idVec3& forwardVec,
												trace_t& out_trace
											  );

	/*!
	* Given a trace which resulted in a detected mantle
	* target, this method checks to see if the target
	* is mantleable.  It looks for a surface up from gravity
	* on which the player can fit while crouching. It also
	* checks that the distance does not violate horizontal
	* and vertical displacement rules for mantling. Finally
	* it checks that the path to the mantleable surface is
	* not blocked by obstructions.
	*
	* This method calls DetermineIfMantleTargetHasMantleableSurface and
	* also DetermineIfPathToMantleSurfaceIsPossible
	*
	* @param[in] maxVerticalReachDistance The maximum distance from the player's origin that the player can reach vertically
	* @param[in] maxHorizontalReachDistance The maximum distance from the player's origin that the player can reach horizontally
	* @param[in] eyePos The position of the player's eyes (camera point) in the world coordinate system
	* @param[in] in_targetTraceResult Pass in the trace result from MantleTargetTrace
	* @param[out] out_mantleEndPoint If the return value is true, this passes back out what the player's origin will be at the end of the mantle
	*i
	* @returns the result of the test
	* @retval true if the mantle target can be mantled
	* @retval false if the mantle target cannot be mantled
	*
	*/
	bool 					ComputeMantlePathForTarget ( float maxVerticalReachDistance,
														 float maxHorizontalReachDistance,
														 const idVec3& eye,
														 trace_t& in_targetTraceResult,
														 idVec3& out_mantleEndPoint
													   );
	/*!
	*
	* This function checks the collision target of the mantle
	* trace to see if there is a surface within reach distance
	* upon which the player will fit.
	*
	* @param[in] maxVerticalReachDistance The maximum distance that the player can reach vertically from their current origin
	* @param[in] maxHorizontalReachDistance The maximum distance that the player can reach horizontally from their current origin
	* @param[in] in_targetTraceResult The trace which found the mantle target
	* @param[out] out_mantleEndPoint If the return code is true, this out paramter specifies the position of the player's origin at the end of the mantle move.
	*
	* @return the result of the test
	* @retval true if the mantle target has a mantleable surface
	* @retval false if the mantel target does not have a mantleable surface
	*
	*/
	bool 					DetermineIfMantleTargetHasMantleableSurface ( float maxVerticalReachDistance,
																		  float maxHorizontalReachDistance,
																		  trace_t& in_targetTraceResult,
																		  idVec3& out_mantleEndPoint
																		);

	/*!
	* Call this method to test whether or not the path
	* along the mantle movement is free of obstructions.
	*
	* @param[in] maxVerticalReachDistance The maximum distance that the player can reach vertically from their current origin
	* @param[in] maxHorizontalReachDistance The maximum distance that the player can reach horizontally from their current origin
	* @param[in] eyePos The position of the player's eyes in the world
	* @param[in] mantleStartPoint The player's origin at the start of the mantle movement
	* @param[in] mantleEndPoint The player's origin at the end of the mantle movement
	*
	* @return the result of the test
	* @retval true if the path is clear
	* @retval false if the path is blocked
	*/
	bool 					DetermineIfPathToMantleSurfaceIsPossible ( float maxVerticalReachDistance,
																	   float maxHorizontalReachDistance,
																	   const idVec3& eyePos,
																	   const idVec3& mantleStartPoint,
																	   const idVec3& mantleEndPoint
																	 );
	/*!
	* This method determines the mantle time required for each phase of the mantle.
	* I made this a function so you could implement things such as carry-weight,
	* tiredness, length of lift....
	* @param[in] mantlePhase The mantle phase for which the duration is to be retrieved
	*/
	float 					GetMantleTimeForPhase ( EMantlePhase mantlePhase );

	/*!
	* This handles the reduction of the mantle timer by the
	* number of milliseconds between animation frames. It
	* uses the timer results to update the mantle timers.
	*/
	void 					UpdateMantleTimers();

	/*!
	* This handles the movement of the the player during
	* the phases of the mantle.  It performs the translation
	* of the player along the mantle path, the camera movement
	* that creates the visual effect, and the placing of the
	* player into a crouch during the end phase of the mantle.
	*
	*/
	void 					MantleMove();

	// Tests if player is holding down jump while already jumping
	// (can be used to trigger mantle)
	bool 					CheckJumpHeldDown();


	bool					DoWeDodge( void );

	idVec3					movementFlow; //normal of the current movement and length of the speed
	idVec3					lastMovementFlow; //used for detecting heavy turning rate

	int						TestConeAlignment( idVec3 vec1, idVec3 vec2, float angle_threshold); //this should be in maths
	float 					MinNormalizeMax( float number, float max, float min );

	bool					elegibleForSkim;
	float					nextSkimTime;
	bool					AreWeTurning( float max_angle );
	bool 					EligibleToSkim( void ); //test when not crouching
	bool					DoWeSkim( void ); // definitive text when crouching
	bool					DoWekeepSkimming( void );

	int 					skim_move_iterations = 0;
	void					StartSkim( void );

	SkimmingPhase 			skimPhase;
	SkimmingPhase			lastSkimPhaseIteration; //this should be useful for not repeating the same messages over and over
	idVec3 					skimmingDir_forward; // the direction of the skimming motion
	idVec3					skimmingDir_right;
	idVec3					skimmingDir_up;
	float					idealFrictionMultiplier;
	float					currentFrictionMultiplier;

	void					UpdateSkimFSM( void );
	void					CorrectDir( idVec3 new_up, idVec3 old_up, idVec3 &dir_up, idVec3 &dir_forward, idVec3 &dir_right );
	idVec3					GetControlFlow( void );

private:
	// player physics state
	playerPState_t			current;
	playerPState_t			saved;

	// properties
	float					walkSpeed;
	float					crouchSpeed;
	float					maxStepHeight;
	float					maxJumpHeight;
	int						debugLevel;				// if set, diagnostic output will be printed

	// player input
	usercmd_t				command;
	idAngles				viewAngles;

	// run-time variables
	int						framemsec;
	float					frametime;
	float					playerSpeed;
	idVec3					viewForward;
	idVec3					viewRight;

	// walk movement
	bool					walking;
	bool					groundPlane;
	trace_t					groundTrace;
	const idMaterial *		groundMaterial;

	// ladder movement
	bool					ladder;
	idVec3					ladderNormal;

	// results of last evaluate
	waterLevel_t			waterLevel;
	int						waterType;

private:
	float					CmdScale( const usercmd_t &cmd ) const;
	void					Accelerate( const idVec3 &wishdir, const float wishspeed, const float accel );
	bool					SlideMove( bool gravity, bool stepUp, bool stepDown, bool push );
	void					Friction( void );
	void					WaterJumpMove( void );
	void					WaterMove( void );
	void					FlyMove( void );
	void					AirMove( void );
	void					WalkMove( void );
	void					DeadMove( void );
	void					NoclipMove( void );
	void					SpectatorMove( void );
	void					LadderMove( void );
	void					SkimMove( void );
	void					CorrectAllSolid( trace_t &trace, int contents );
	void					CheckGround( void );
	void					CheckDuck( void );
	void					CheckLadder( void );
	bool					CheckJump( void );
	bool					CheckWaterJump( void );
	bool					CheckSkimHit( void );
	void					SetWaterLevel( void );
	void					DropTimers( void );
	void					MovePlayer( int msec );
};

#endif /* !__PHYSICS_PLAYER_H__ */
