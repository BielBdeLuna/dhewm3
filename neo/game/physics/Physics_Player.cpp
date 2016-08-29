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

#include "sys/platform.h"
#include "gamesys/SysCvar.h"
#include "Entity.h"
#include "../Player.h"
#include "physics/Physics_Player.h"

CLASS_DECLARATION( idPhysics_Actor, idPhysics_Player )
END_CLASS

// movement parameters
const float PM_STOPSPEED		= 100.0f;
const float PM_SWIMSCALE		= 0.5f;
const float PM_LADDERSPEED		= 100.0f;
const float PM_STEPSCALE		= 1.0f;

const float PM_ACCELERATE		= 10.0f;
const float PM_AIRACCELERATE	= 1.0f;
const float PM_WATERACCELERATE	= 4.0f;
const float PM_FLYACCELERATE	= 8.0f;

const float PM_FRICTION			= 6.0f;
const float PM_AIRFRICTION		= 0.0f;
const float PM_WATERFRICTION	= 1.0f;
const float PM_FLYFRICTION		= 3.0f;
const float PM_NOCLIPFRICTION	= 12.0f;

/**
*  Height unit increment for mantle test
* This value should be >= 1.0
* A larger value reduces the number of tests during a mantle
* initiation, but may not find some small mantleable "nooks"
* in a surface.
**/
const float MANTLE_TEST_INCREMENT = 1.0;

const float MIN_WALK_NORMAL		= 0.7f;		// can't walk on very steep slopes
const float OVERCLIP			= 1.001f;

// movementFlags
const int PMF_DUCKED			= 1;		// set when ducking
const int PMF_JUMPED			= 2;		// set when the player jumped this frame
const int PMF_STEPPED_UP		= 4;		// set when the player stepped up this frame
const int PMF_STEPPED_DOWN		= 8;		// set when the player stepped down this frame
const int PMF_JUMP_HELD			= 16;		// set when jump button is held down
const int PMF_TIME_LAND			= 32;		// movementTime is time before rejump
const int PMF_TIME_KNOCKBACK	= 64;		// movementTime is an air-accelerate only time
const int PMF_TIME_WATERJUMP	= 128;		// movementTime is waterjump
const int PMF_ALL_TIMES			= (PMF_TIME_WATERJUMP|PMF_TIME_LAND|PMF_TIME_KNOCKBACK);

int c_pmove = 0;

/*
============
idPhysics_Player::CmdScale

Returns the scale factor to apply to cmd movements
This allows the clients to use axial -127 to 127 values for all directions
without getting a sqrt(2) distortion in speed.
============
*/
float idPhysics_Player::CmdScale( const usercmd_t &cmd ) const {
	int		max;
	float	total;
	float	scale;
	int		forwardmove;
	int		rightmove;
	int		upmove;

	forwardmove = cmd.forwardmove;
	rightmove = cmd.rightmove;

	// since the crouch key doubles as downward movement, ignore downward movement when we're on the ground
	// otherwise crouch speed will be lower than specified
	if ( walking ) {
		upmove = 0;
	} else {
		upmove = cmd.upmove;
	}

	max = abs( forwardmove );
	if ( abs( rightmove ) > max ) {
		max = abs( rightmove );
	}
	if ( abs( upmove ) > max ) {
		max = abs( upmove );
	}

	if ( !max ) {
		return 0.0f;
	}

	total = idMath::Sqrt( (float) forwardmove * forwardmove + rightmove * rightmove + upmove * upmove );
	scale = (float) playerSpeed * max / ( 127.0f * total );

	return scale;
}

/*
==============
idPhysics_Player::Accelerate

Handles user intended acceleration
==============
*/
void idPhysics_Player::Accelerate( const idVec3 &wishdir, const float wishspeed, const float accel ) {
#if 1
	// q2 style
	float addspeed, accelspeed, currentspeed;

	currentspeed = current.velocity * wishdir;
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0) {
		return;
	}
	accelspeed = accel * frametime * wishspeed;
	if (accelspeed > addspeed) {
		accelspeed = addspeed;
	}

	current.velocity += accelspeed * wishdir;
#else
	// proper way (avoids strafe jump maxspeed bug), but feels bad
	idVec3		wishVelocity;
	idVec3		pushDir;
	float		pushLen;
	float		canPush;

	wishVelocity = wishdir * wishspeed;
	pushDir = wishVelocity - current.velocity;
	pushLen = pushDir.Normalize();

	canPush = accel * frametime * wishspeed;
	if (canPush > pushLen) {
		canPush = pushLen;
	}

	current.velocity += canPush * pushDir;
#endif
}

/*
==================
idPhysics_Player::SlideMove

Returns true if the velocity was clipped in some way
==================
*/
#define	MAX_CLIP_PLANES	5

bool idPhysics_Player::SlideMove( bool gravity, bool stepUp, bool stepDown, bool push ) {
	int			i, j, k, pushFlags;
	int			bumpcount, numbumps, numplanes;
	float		d, time_left, into, totalMass;
	idVec3		dir, planes[MAX_CLIP_PLANES];
	idVec3		end, stepEnd, primal_velocity, endVelocity, endClipVelocity, clipVelocity;
	trace_t		trace, stepTrace, downTrace;
	bool		nearGround, stepped, pushed;

	numbumps = 4;

	primal_velocity = current.velocity;

	if ( gravity ) {
		endVelocity = current.velocity + gravityVector * frametime;
		current.velocity = ( current.velocity + endVelocity ) * 0.5f;
		primal_velocity = endVelocity;
		if ( groundPlane ) {
			// slide along the ground plane
			current.velocity.ProjectOntoPlane( groundTrace.c.normal, OVERCLIP );
		}
	}
	else {
		endVelocity = current.velocity;
	}

	time_left = frametime;

	// never turn against the ground plane
	if ( groundPlane ) {
		numplanes = 1;
		planes[0] = groundTrace.c.normal;
	} else {
		numplanes = 0;
	}

	// never turn against original velocity
	planes[numplanes] = current.velocity;
	planes[numplanes].Normalize();
	numplanes++;

	for ( bumpcount = 0; bumpcount < numbumps; bumpcount++ ) {

		// calculate position we are trying to move to
		end = current.origin + time_left * current.velocity;

		// see if we can make it there
		gameLocal.clip.Translation( trace, current.origin, end, clipModel, clipModel->GetAxis(), clipMask, self );

		time_left -= time_left * trace.fraction;
		current.origin = trace.endpos;

		// if moved the entire distance
		if ( trace.fraction >= 1.0f ) {
			break;
		}

		stepped = pushed = false;

		// if we are allowed to step up
		if ( stepUp ) {

			nearGround = groundPlane | ladder;

			if ( !nearGround ) {
				// trace down to see if the player is near the ground
				// step checking when near the ground allows the player to move up stairs smoothly while jumping
				stepEnd = current.origin + maxStepHeight * gravityNormal;
				gameLocal.clip.Translation( downTrace, current.origin, stepEnd, clipModel, clipModel->GetAxis(), clipMask, self );
				nearGround = ( downTrace.fraction < 1.0f && (downTrace.c.normal * -gravityNormal) > MIN_WALK_NORMAL );
			}

			// may only step up if near the ground or on a ladder
			if ( nearGround ) {

				// step up
				stepEnd = current.origin - maxStepHeight * gravityNormal;
				gameLocal.clip.Translation( downTrace, current.origin, stepEnd, clipModel, clipModel->GetAxis(), clipMask, self );

				// trace along velocity
				stepEnd = downTrace.endpos + time_left * current.velocity;
				gameLocal.clip.Translation( stepTrace, downTrace.endpos, stepEnd, clipModel, clipModel->GetAxis(), clipMask, self );

				// step down
				stepEnd = stepTrace.endpos + maxStepHeight * gravityNormal;
				gameLocal.clip.Translation( downTrace, stepTrace.endpos, stepEnd, clipModel, clipModel->GetAxis(), clipMask, self );

				if ( downTrace.fraction >= 1.0f || (downTrace.c.normal * -gravityNormal) > MIN_WALK_NORMAL ) {

					// if moved the entire distance
					if ( stepTrace.fraction >= 1.0f ) {
						time_left = 0;
						current.stepUp -= ( downTrace.endpos - current.origin ) * gravityNormal;
						current.origin = downTrace.endpos;
						current.movementFlags |= PMF_STEPPED_UP;
						current.velocity *= PM_STEPSCALE;
						break;
					}

					// if the move is further when stepping up
					if ( stepTrace.fraction > trace.fraction ) {
						time_left -= time_left * stepTrace.fraction;
						current.stepUp -= ( downTrace.endpos - current.origin ) * gravityNormal;
						current.origin = downTrace.endpos;
						current.movementFlags |= PMF_STEPPED_UP;
						current.velocity *= PM_STEPSCALE;
						trace = stepTrace;
						stepped = true;
					}
				}
			}
		}

		// if we can push other entities and not blocked by the world
		if ( push && trace.c.entityNum != ENTITYNUM_WORLD ) {

			clipModel->SetPosition( current.origin, clipModel->GetAxis() );

			// clip movement, only push idMoveables, don't push entities the player is standing on
			// apply impact to pushed objects
			pushFlags = PUSHFL_CLIP|PUSHFL_ONLYMOVEABLE|PUSHFL_NOGROUNDENTITIES|PUSHFL_APPLYIMPULSE;

			// clip & push
			totalMass = gameLocal.push.ClipTranslationalPush( trace, self, pushFlags, end, end - current.origin );

			if ( totalMass > 0.0f ) {
				// decrease velocity based on the total mass of the objects being pushed ?
				current.velocity *= 1.0f - idMath::ClampFloat( 0.0f, 1000.0f, totalMass - 20.0f ) * ( 1.0f / 950.0f );
				pushed = true;
			}

			current.origin = trace.endpos;
			time_left -= time_left * trace.fraction;

			// if moved the entire distance
			if ( trace.fraction >= 1.0f ) {
				break;
			}
		}

		if ( !stepped ) {
			// let the entity know about the collision
			self->Collide( trace, current.velocity );
		}

		if ( numplanes >= MAX_CLIP_PLANES ) {
			// MrElusive: I think we have some relatively high poly LWO models with a lot of slanted tris
			// where it may hit the max clip planes
			current.velocity = vec3_origin;
			return true;
		}

		//
		// if this is the same plane we hit before, nudge velocity
		// out along it, which fixes some epsilon issues with
		// non-axial planes
		//
		for ( i = 0; i < numplanes; i++ ) {
			if ( ( trace.c.normal * planes[i] ) > 0.999f ) {
				current.velocity += trace.c.normal;
				break;
			}
		}
		if ( i < numplanes ) {
			continue;
		}
		planes[numplanes] = trace.c.normal;
		numplanes++;

		//
		// modify velocity so it parallels all of the clip planes
		//

		// find a plane that it enters
		for ( i = 0; i < numplanes; i++ ) {
			into = current.velocity * planes[i];
			if ( into >= 0.1f ) {
				continue;		// move doesn't interact with the plane
			}

			// slide along the plane
			clipVelocity = current.velocity;
			clipVelocity.ProjectOntoPlane( planes[i], OVERCLIP );

			// slide along the plane
			endClipVelocity = endVelocity;
			endClipVelocity.ProjectOntoPlane( planes[i], OVERCLIP );

			// see if there is a second plane that the new move enters
			for ( j = 0; j < numplanes; j++ ) {
				if ( j == i ) {
					continue;
				}
				if ( ( clipVelocity * planes[j] ) >= 0.1f ) {
					continue;		// move doesn't interact with the plane
				}

				// try clipping the move to the plane
				clipVelocity.ProjectOntoPlane( planes[j], OVERCLIP );
				endClipVelocity.ProjectOntoPlane( planes[j], OVERCLIP );

				// see if it goes back into the first clip plane
				if ( ( clipVelocity * planes[i] ) >= 0 ) {
					continue;
				}

				// slide the original velocity along the crease
				dir = planes[i].Cross( planes[j] );
				dir.Normalize();
				d = dir * current.velocity;
				clipVelocity = d * dir;

				dir = planes[i].Cross( planes[j] );
				dir.Normalize();
				d = dir * endVelocity;
				endClipVelocity = d * dir;

				// see if there is a third plane the the new move enters
				for ( k = 0; k < numplanes; k++ ) {
					if ( k == i || k == j ) {
						continue;
					}
					if ( ( clipVelocity * planes[k] ) >= 0.1f ) {
						continue;		// move doesn't interact with the plane
					}

					// stop dead at a tripple plane interaction
					current.velocity = vec3_origin;
					return true;
				}
			}

			// if we have fixed all interactions, try another move
			current.velocity = clipVelocity;
			endVelocity = endClipVelocity;
			break;
		}
	}

	// step down
	if ( stepDown && groundPlane ) {
		stepEnd = current.origin + gravityNormal * maxStepHeight;
		gameLocal.clip.Translation( downTrace, current.origin, stepEnd, clipModel, clipModel->GetAxis(), clipMask, self );
		if ( downTrace.fraction > 1e-4f && downTrace.fraction < 1.0f ) {
			current.stepUp -= ( downTrace.endpos - current.origin ) * gravityNormal;
			current.origin = downTrace.endpos;
			current.movementFlags |= PMF_STEPPED_DOWN;
			current.velocity *= PM_STEPSCALE;
		}
	}

	if ( gravity ) {
		current.velocity = endVelocity;
	}

	// come to a dead stop when the velocity orthogonal to the gravity flipped
	clipVelocity = current.velocity - gravityNormal * current.velocity * gravityNormal;
	endClipVelocity = endVelocity - gravityNormal * endVelocity * gravityNormal;
	if ( clipVelocity * endClipVelocity < 0.0f ) {
		current.velocity = gravityNormal * current.velocity * gravityNormal;
	}

	return (bool)( bumpcount == 0 );
}

/*
==================
idPhysics_Player::Friction

Handles both ground friction and water friction
==================
*/
void idPhysics_Player::Friction( void ) {
	idVec3	vel;
	float	speed, newspeed, control;
	float	drop;

	vel = current.velocity;
	if ( walking ) {
		// ignore slope movement, remove all velocity in gravity direction
		vel += (vel * gravityNormal) * gravityNormal;
	}

	speed = vel.Length();
	if ( speed < 1.0f ) {
		// remove all movement orthogonal to gravity, allows for sinking underwater
		if ( fabs( current.velocity * gravityNormal ) < 1e-5f ) {
			current.velocity.Zero();
		} else {
			current.velocity = (current.velocity * gravityNormal) * gravityNormal;
		}
		// FIXME: still have z friction underwater?
		return;
	}

	drop = 0;

	// spectator friction
	if ( current.movementType == PM_SPECTATOR ) {
		drop += speed * PM_FLYFRICTION * frametime;
	}
	// apply ground friction
	else if ( walking && waterLevel <= WATERLEVEL_FEET ) {
		// no friction on slick surfaces
		if ( !(groundMaterial && groundMaterial->GetSurfaceFlags() & SURF_SLICK) ) {
			// if getting knocked back, no friction
			if ( !(current.movementFlags & PMF_TIME_KNOCKBACK) ) {
				control = speed < PM_STOPSPEED ? PM_STOPSPEED : speed;
				drop += control * PM_FRICTION * frametime;
			}
		}
	}
	// apply water friction even if just wading
	else if ( waterLevel ) {
		drop += speed * PM_WATERFRICTION * waterLevel * frametime;
	}
	// apply air friction
	else {
		drop += speed * PM_AIRFRICTION * frametime;
	}

	// scale the velocity
	newspeed = speed - drop;
	if (newspeed < 0) {
		newspeed = 0;
	}
	current.velocity *= ( newspeed / speed );
}

/*
===================
idPhysics_Player::WaterJumpMove

Flying out of the water
===================
*/
void idPhysics_Player::WaterJumpMove( void ) {

	// waterjump has no control, but falls
	idPhysics_Player::SlideMove( true, true, false, false );

	// add gravity
	current.velocity += gravityNormal * frametime;
	// if falling down
	if ( current.velocity * gravityNormal > 0.0f ) {
		// cancel as soon as we are falling down again
		current.movementFlags &= ~PMF_ALL_TIMES;
		current.movementTime = 0;
	}
}

/*
===================
idPhysics_Player::WaterMove
===================
*/
void idPhysics_Player::WaterMove( void ) {
	idVec3	wishvel;
	float	wishspeed;
	idVec3	wishdir;
	float	scale;
	float	vel;

	if ( idPhysics_Player::CheckWaterJump() ) {
		idPhysics_Player::WaterJumpMove();
		return;
	}

	idPhysics_Player::Friction();

	scale = idPhysics_Player::CmdScale( command );

	// user intentions
	if ( !scale ) {
		wishvel = gravityNormal * 60; // sink towards bottom
	} else {
		wishvel = scale * (viewForward * command.forwardmove + viewRight * command.rightmove);
		wishvel -= scale * gravityNormal * command.upmove;
	}

	wishdir = wishvel;
	wishspeed = wishdir.Normalize();

	if ( wishspeed > playerSpeed * PM_SWIMSCALE ) {
		wishspeed = playerSpeed * PM_SWIMSCALE;
	}

	idPhysics_Player::Accelerate( wishdir, wishspeed, PM_WATERACCELERATE );

	// make sure we can go up slopes easily under water
	if ( groundPlane && ( current.velocity * groundTrace.c.normal ) < 0.0f ) {
		vel = current.velocity.Length();
		// slide along the ground plane
		current.velocity.ProjectOntoPlane( groundTrace.c.normal, OVERCLIP );

		current.velocity.Normalize();
		current.velocity *= vel;
	}

	idPhysics_Player::SlideMove( false, true, false, false );
}

/*
===================
idPhysics_Player::FlyMove
===================
*/
void idPhysics_Player::FlyMove( void ) {
	idVec3	wishvel;
	float	wishspeed;
	idVec3	wishdir;
	float	scale;

	// normal slowdown
	idPhysics_Player::Friction();

	scale = idPhysics_Player::CmdScale( command );

	if ( !scale ) {
		wishvel = vec3_origin;
	} else {
		wishvel = scale * (viewForward * command.forwardmove + viewRight * command.rightmove);
		wishvel -= scale * gravityNormal * command.upmove;
	}

	wishdir = wishvel;
	wishspeed = wishdir.Normalize();

	idPhysics_Player::Accelerate( wishdir, wishspeed, PM_FLYACCELERATE );

	idPhysics_Player::SlideMove( false, false, false, false );
}

/*
===================
idPhysics_Player::AirMove
===================
*/
void idPhysics_Player::AirMove( void ) {
	idVec3		wishvel;
	idVec3		wishdir;
	float		wishspeed;
	float		scale;

	idPhysics_Player::Friction();

	scale = idPhysics_Player::CmdScale( command );

	// project moves down to flat plane
	viewForward -= (viewForward * gravityNormal) * gravityNormal;
	viewRight -= (viewRight * gravityNormal) * gravityNormal;
	viewForward.Normalize();
	viewRight.Normalize();

	wishvel = viewForward * command.forwardmove + viewRight * command.rightmove;
	wishvel -= (wishvel * gravityNormal) * gravityNormal;
	wishdir = wishvel;
	wishspeed = wishdir.Normalize();
	wishspeed *= scale;

	// not on ground, so little effect on velocity
	idPhysics_Player::Accelerate( wishdir, wishspeed, PM_AIRACCELERATE );

	// we may have a ground plane that is very steep, even
	// though we don't have a groundentity
	// slide along the steep plane
	if ( groundPlane ) {
		current.velocity.ProjectOntoPlane( groundTrace.c.normal, OVERCLIP );
	}

	idPhysics_Player::SlideMove( true, false, false, false );
}

/*
===================
idPhysics_Player::WalkMove
===================
*/
void idPhysics_Player::WalkMove( void ) {
	idVec3		wishvel;
	idVec3		wishdir;
	float		wishspeed;
	float		scale;
	float		accelerate;
	idVec3		oldVelocity, vel;
	float		oldVel, newVel;

	if ( waterLevel > WATERLEVEL_WAIST && ( viewForward * groundTrace.c.normal ) > 0.0f ) {
		// begin swimming
		idPhysics_Player::WaterMove();
		return;
	}

	if ( idPhysics_Player::CheckJump() ) {
		// jumped away
		if ( waterLevel > WATERLEVEL_FEET ) {
			idPhysics_Player::WaterMove();
		}
		else {
			idPhysics_Player::AirMove();
		}
		return;
	}

	idPhysics_Player::Friction();

	scale = idPhysics_Player::CmdScale( command );

	// project moves down to flat plane
	viewForward -= (viewForward * gravityNormal) * gravityNormal;
	viewRight -= (viewRight * gravityNormal) * gravityNormal;

	// project the forward and right directions onto the ground plane
	viewForward.ProjectOntoPlane( groundTrace.c.normal, OVERCLIP );
	viewRight.ProjectOntoPlane( groundTrace.c.normal, OVERCLIP );
	//
	viewForward.Normalize();
	viewRight.Normalize();

	wishvel = viewForward * command.forwardmove + viewRight * command.rightmove;
	wishdir = wishvel;
	wishspeed = wishdir.Normalize();
	wishspeed *= scale;

	// clamp the speed lower if wading or walking on the bottom
	if ( waterLevel ) {
		float	waterScale;

		waterScale = waterLevel / 3.0f;
		waterScale = 1.0f - ( 1.0f - PM_SWIMSCALE ) * waterScale;
		if ( wishspeed > playerSpeed * waterScale ) {
			wishspeed = playerSpeed * waterScale;
		}
	}

	// when a player gets hit, they temporarily lose full control, which allows them to be moved a bit
	if ( ( groundMaterial && groundMaterial->GetSurfaceFlags() & SURF_SLICK ) || current.movementFlags & PMF_TIME_KNOCKBACK ) {
		accelerate = PM_AIRACCELERATE;
	}
	else {
		accelerate = PM_ACCELERATE;
	}

	idPhysics_Player::Accelerate( wishdir, wishspeed, accelerate );

	if ( ( groundMaterial && groundMaterial->GetSurfaceFlags() & SURF_SLICK ) || current.movementFlags & PMF_TIME_KNOCKBACK ) {
		current.velocity += gravityVector * frametime;
	}

	oldVelocity = current.velocity;

	// slide along the ground plane
	current.velocity.ProjectOntoPlane( groundTrace.c.normal, OVERCLIP );

	// if not clipped into the opposite direction
	if ( oldVelocity * current.velocity > 0.0f ) {
		newVel = current.velocity.LengthSqr();
		if ( newVel > 1.0f ) {
			oldVel = oldVelocity.LengthSqr();
			if ( oldVel > 1.0f ) {
				// don't decrease velocity when going up or down a slope
				current.velocity *= idMath::Sqrt( oldVel / newVel );
			}
		}
	}

	// don't do anything if standing still
	vel = current.velocity - (current.velocity * gravityNormal) * gravityNormal;
	if ( !vel.LengthSqr() ) {
		return;
	}

	gameLocal.push.InitSavingPushedEntityPositions();

	idPhysics_Player::SlideMove( false, true, true, true );
}

/*
==============
idPhysics_Player::DeadMove
==============
*/
void idPhysics_Player::DeadMove( void ) {
	float	forward;

	if ( !walking ) {
		return;
	}

	// extra friction
	forward = current.velocity.Length();
	forward -= 20;
	if ( forward <= 0 ) {
		current.velocity = vec3_origin;
	}
	else {
		current.velocity.Normalize();
		current.velocity *= forward;
	}
}

/*
===============
idPhysics_Player::NoclipMove
===============
*/
void idPhysics_Player::NoclipMove( void ) {
	float		speed, drop, friction, newspeed, stopspeed;
	float		scale, wishspeed;
	idVec3		wishdir;

	// friction
	speed = current.velocity.Length();
	if ( speed < 20.0f ) {
		current.velocity = vec3_origin;
	}
	else {
		stopspeed = playerSpeed * 0.3f;
		if ( speed < stopspeed ) {
			speed = stopspeed;
		}
		friction = PM_NOCLIPFRICTION;
		drop = speed * friction * frametime;

		// scale the velocity
		newspeed = speed - drop;
		if (newspeed < 0) {
			newspeed = 0;
		}

		current.velocity *= newspeed / speed;
	}

	// accelerate
	scale = idPhysics_Player::CmdScale( command );

	wishdir = scale * (viewForward * command.forwardmove + viewRight * command.rightmove);
	wishdir -= scale * gravityNormal * command.upmove;
	wishspeed = wishdir.Normalize();
	wishspeed *= scale;

	idPhysics_Player::Accelerate( wishdir, wishspeed, PM_ACCELERATE );

	// move
	current.origin += frametime * current.velocity;
}

/*
===============
idPhysics_Player::SpectatorMove
===============
*/
void idPhysics_Player::SpectatorMove( void ) {
	idVec3	wishvel;
	float	wishspeed;
	idVec3	wishdir;
	float	scale;

	idVec3	end;

	// fly movement

	idPhysics_Player::Friction();

	scale = idPhysics_Player::CmdScale( command );

	if ( !scale ) {
		wishvel = vec3_origin;
	} else {
		wishvel = scale * (viewForward * command.forwardmove + viewRight * command.rightmove);
	}

	wishdir = wishvel;
	wishspeed = wishdir.Normalize();

	idPhysics_Player::Accelerate( wishdir, wishspeed, PM_FLYACCELERATE );

	idPhysics_Player::SlideMove( false, false, false, false );
}

/*
============
idPhysics_Player::LadderMove
============
*/
void idPhysics_Player::LadderMove( void ) {
	idVec3	wishdir, wishvel, right;
	float	wishspeed, scale;
	float	upscale;

	// stick to the ladder
	wishvel = -100.0f * ladderNormal;
	current.velocity = (gravityNormal * current.velocity) * gravityNormal + wishvel;

	upscale = (-gravityNormal * viewForward + 0.5f) * 2.5f;
	if ( upscale > 1.0f ) {
		upscale = 1.0f;
	}
	else if ( upscale < -1.0f ) {
		upscale = -1.0f;
	}

	scale = idPhysics_Player::CmdScale( command );
	wishvel = -0.9f * gravityNormal * upscale * scale * (float)command.forwardmove;

	// strafe
	if ( command.rightmove ) {
		// right vector orthogonal to gravity
		right = viewRight - (gravityNormal * viewRight) * gravityNormal;
		// project right vector into ladder plane
		right = right - (ladderNormal * right) * ladderNormal;
		right.Normalize();

		// if we are looking away from the ladder, reverse the right vector
		if ( ladderNormal * viewForward > 0.0f ) {
			right = -right;
		}
		wishvel += 2.0f * right * scale * (float) command.rightmove;
	}

	// up down movement
	if ( command.upmove ) {
		wishvel += -0.5f * gravityNormal * scale * (float) command.upmove;
	}

	// do strafe friction
	idPhysics_Player::Friction();

	// accelerate
	wishspeed = wishvel.Normalize();
	idPhysics_Player::Accelerate( wishvel, wishspeed, PM_ACCELERATE );

	// cap the vertical velocity
	upscale = current.velocity * -gravityNormal;
	if ( upscale < -PM_LADDERSPEED ) {
		current.velocity += gravityNormal * (upscale + PM_LADDERSPEED);
	}
	else if ( upscale > PM_LADDERSPEED ) {
		current.velocity += gravityNormal * (upscale - PM_LADDERSPEED);
	}

	if ( (wishvel * gravityNormal) == 0.0f ) {
		if ( current.velocity * gravityNormal < 0.0f ) {
			current.velocity += gravityVector * frametime;
			if ( current.velocity * gravityNormal > 0.0f ) {
				current.velocity -= (gravityNormal * current.velocity) * gravityNormal;
			}
		}
		else {
			current.velocity -= gravityVector * frametime;
			if ( current.velocity * gravityNormal < 0.0f ) {
				current.velocity -= (gravityNormal * current.velocity) * gravityNormal;
			}
		}
	}

	idPhysics_Player::SlideMove( false, ( command.forwardmove > 0 ), false, false );
}

/*
=============
idPhysics_Player::CorrectAllSolid
=============
*/
void idPhysics_Player::CorrectAllSolid( trace_t &trace, int contents ) {
	if ( debugLevel ) {
		gameLocal.Printf( "%i:allsolid\n", c_pmove );
	}

	// FIXME: jitter around to find a free spot ?

	if ( trace.fraction >= 1.0f ) {
		memset( &trace, 0, sizeof( trace ) );
		trace.endpos = current.origin;
		trace.endAxis = clipModelAxis;
		trace.fraction = 0.0f;
		trace.c.dist = current.origin.z;
		trace.c.normal.Set( 0, 0, 1 );
		trace.c.point = current.origin;
		trace.c.entityNum = ENTITYNUM_WORLD;
		trace.c.id = 0;
		trace.c.type = CONTACT_TRMVERTEX;
		trace.c.material = NULL;
		trace.c.contents = contents;
	}
}

/*
=============
idPhysics_Player::CheckGround
=============
*/
void idPhysics_Player::CheckGround( void ) {
	int i, contents;
	idVec3 point;
	bool hadGroundContacts;

	hadGroundContacts = HasGroundContacts();

	// set the clip model origin before getting the contacts
	clipModel->SetPosition( current.origin, clipModel->GetAxis() );

	EvaluateContacts();

	// setup a ground trace from the contacts
	groundTrace.endpos = current.origin;
	groundTrace.endAxis = clipModel->GetAxis();
	if ( contacts.Num() ) {
		groundTrace.fraction = 0.0f;
		groundTrace.c = contacts[0];
		for ( i = 1; i < contacts.Num(); i++ ) {
			groundTrace.c.normal += contacts[i].normal;
		}
		groundTrace.c.normal.Normalize();
	} else {
		groundTrace.fraction = 1.0f;
	}

	contents = gameLocal.clip.Contents( current.origin, clipModel, clipModel->GetAxis(), -1, self );
	if ( contents & MASK_SOLID ) {
		// do something corrective if stuck in solid
		idPhysics_Player::CorrectAllSolid( groundTrace, contents );
	}
	else if ( m_mantlePhase == fixTheClipping )
	{
		// the mantle stage can advance to done if we're not currently clipping
		m_mantlePhase = notMantling;
	}

	// if the trace didn't hit anything, we are in free fall
	if ( groundTrace.fraction == 1.0f ) {
		groundPlane = false;
		walking = false;
		groundEntityPtr = NULL;
		return;
	}

	groundMaterial = groundTrace.c.material;
	groundEntityPtr = gameLocal.entities[ groundTrace.c.entityNum ];

	// check if getting thrown off the ground
	if ( (current.velocity * -gravityNormal) > 0.0f && ( current.velocity * groundTrace.c.normal ) > 10.0f ) {
		if ( debugLevel ) {
			gameLocal.Printf( "%i:kickoff\n", c_pmove );
		}

		groundPlane = false;
		walking = false;
		return;
	}

	// slopes that are too steep will not be considered onground
	if ( ( groundTrace.c.normal * -gravityNormal ) < MIN_WALK_NORMAL ) {
		if ( debugLevel ) {
			gameLocal.Printf( "%i:steep\n", c_pmove );
		}

		// FIXME: if they can't slide down the slope, let them walk (sharp crevices)

		// make sure we don't die from sliding down a steep slope
		if ( current.velocity * gravityNormal > 150.0f ) {
			current.velocity -= ( current.velocity * gravityNormal - 150.0f ) * gravityNormal;
		}

		groundPlane = true;
		walking = false;
		return;
	}

	groundPlane = true;
	walking = true;

	// hitting solid ground will end a waterjump
	if ( current.movementFlags & PMF_TIME_WATERJUMP ) {
		current.movementFlags &= ~( PMF_TIME_WATERJUMP | PMF_TIME_LAND );
		current.movementTime = 0;
	}

	// if the player didn't have ground contacts the previous frame
	if ( !hadGroundContacts ) {

		// don't do landing time if we were just going down a slope
		if ( (current.velocity * -gravityNormal) < -200.0f ) {
			// don't allow another jump for a little while
			current.movementFlags |= PMF_TIME_LAND;
			current.movementTime = 250;
		}
	}

	// let the entity know about the collision
	self->Collide( groundTrace, current.velocity );

	if ( groundEntityPtr.GetEntity() ) {
		impactInfo_t info;
		groundEntityPtr.GetEntity()->GetImpactInfo( self, groundTrace.c.id, groundTrace.c.point, &info );
		if ( info.invMass != 0.0f ) {
			groundEntityPtr.GetEntity()->ApplyImpulse( self, groundTrace.c.id, groundTrace.c.point, current.velocity / ( info.invMass * 10.0f ) );
		}
	}
}

/*
==============
idPhysics_Player::CheckDuck

Sets clip model size
==============
*/
void idPhysics_Player::CheckDuck( void ) {
	trace_t	trace;
	idVec3 end;
	idBounds bounds;
	float maxZ;

	if ( current.movementType == PM_DEAD ) {
		maxZ = pm_deadheight.GetFloat();
	} else {
		// stand up when up against a ladder
		if ( command.upmove < 0 && !ladder ) {
			// duck
			current.movementFlags |= PMF_DUCKED;
		} else {
			// stand up if possible
			if ( current.movementFlags & PMF_DUCKED ) {
				// try to stand up
				end = current.origin - ( pm_normalheight.GetFloat() - pm_crouchheight.GetFloat() ) * gravityNormal;
				gameLocal.clip.Translation( trace, current.origin, end, clipModel, clipModel->GetAxis(), clipMask, self );
				if ( trace.fraction >= 1.0f ) {
					current.movementFlags &= ~PMF_DUCKED;
				}
			}
		}

		if ( current.movementFlags & PMF_DUCKED ) {
			playerSpeed = crouchSpeed;
			maxZ = pm_crouchheight.GetFloat();
		} else {
			maxZ = pm_normalheight.GetFloat();
		}
	}
	// if the clipModel height should change
	if ( clipModel->GetBounds()[1][2] != maxZ ) {

		bounds = clipModel->GetBounds();
		bounds[1][2] = maxZ;
		if ( pm_usecylinder.GetBool() ) {
			clipModel->LoadModel( idTraceModel( bounds, 8 ) );
		} else {
			clipModel->LoadModel( idTraceModel( bounds ) );
		}
	}
}

/*
================
idPhysics_Player::CheckLadder
================
*/
void idPhysics_Player::CheckLadder( void ) {
	idVec3		forward, start, end;
	trace_t		trace;
	float		tracedist;

	if ( current.movementTime ) {
		return;
	}

	// if on the ground moving backwards
	if ( walking && command.forwardmove <= 0 ) {
		return;
	}

	// Don't attach to ropes or ladders in the middle of a mantle
	if ( IsMantling() ) {
		return;
	}

	// forward vector orthogonal to gravity
	forward = viewForward - (gravityNormal * viewForward) * gravityNormal;
	forward.Normalize();

	if ( walking ) {
		// don't want to get sucked towards the ladder when still walking
		tracedist = 1.0f;
	} else {
		tracedist = 48.0f;
	}

	end = current.origin + tracedist * forward;
	gameLocal.clip.Translation( trace, current.origin, end, clipModel, clipModel->GetAxis(), clipMask, self );

	// if near a surface
	if ( trace.fraction < 1.0f ) {

		// if a ladder surface
		if ( trace.c.material && ( trace.c.material->GetSurfaceFlags() & SURF_LADDER ) ) {

			// check a step height higher
			end = current.origin - gravityNormal * ( maxStepHeight * 0.75f );
			gameLocal.clip.Translation( trace, current.origin, end, clipModel, clipModel->GetAxis(), clipMask, self );
			start = trace.endpos;
			end = start + tracedist * forward;
			gameLocal.clip.Translation( trace, start, end, clipModel, clipModel->GetAxis(), clipMask, self );

			// if also near a surface a step height higher
			if ( trace.fraction < 1.0f ) {

				// if it also is a ladder surface
				if ( trace.c.material && trace.c.material->GetSurfaceFlags() & SURF_LADDER ) {
					ladder = true;
					ladderNormal = trace.c.normal;
				}
			}
		}
	}
}

/*
=============
idPhysics_Player::CheckJump
=============
*/
bool idPhysics_Player::CheckJump( void ) {
	idVec3 addVelocity;

	if ( command.upmove < 10 ) {
		// not holding jump
		return false;
	}

	// must wait for jump to be released
	if ( current.movementFlags & PMF_JUMP_HELD ) {
		return false;
	}

	// don't jump if we can't stand up
	if ( current.movementFlags & PMF_DUCKED ) {
		return false;
	}

	groundPlane = false;		// jumping away
	walking = false;
	current.movementFlags |= PMF_JUMP_HELD | PMF_JUMPED;

	addVelocity = 2.0f * maxJumpHeight * -gravityVector;
	addVelocity *= idMath::Sqrt( addVelocity.Normalize() );
	current.velocity += addVelocity;

	return true;
}

/*
=============
idPhysics_Player::CheckWaterJump
=============
*/
bool idPhysics_Player::CheckWaterJump( void ) {
	idVec3	spot;
	int		cont;
	idVec3	flatforward;

	if ( current.movementTime ) {
		return false;
	}

	// check for water jump
	if ( waterLevel != WATERLEVEL_WAIST ) {
		return false;
	}

	flatforward = viewForward - (viewForward * gravityNormal) * gravityNormal;
	flatforward.Normalize();

	spot = current.origin + 30.0f * flatforward;
	spot -= 4.0f * gravityNormal;
	cont = gameLocal.clip.Contents( spot, NULL, mat3_identity, -1, self );
	if ( !(cont & CONTENTS_SOLID) ) {
		return false;
	}

	spot -= 16.0f * gravityNormal;
	cont = gameLocal.clip.Contents( spot, NULL, mat3_identity, -1, self );
	if ( cont ) {
		return false;
	}

	// jump out of water
	current.velocity = 200.0f * viewForward - 350.0f * gravityNormal;
	current.movementFlags |= PMF_TIME_WATERJUMP;
	current.movementTime = 2000;

	return true;
}

/*
=============
idPhysics_Player::SetWaterLevel
=============
*/
void idPhysics_Player::SetWaterLevel( void ) {
	idVec3		point;
	idBounds	bounds;
	int			contents;

	//
	// get waterlevel, accounting for ducking
	//
	waterLevel = WATERLEVEL_NONE;
	waterType = 0;

	bounds = clipModel->GetBounds();

	// check at feet level
	point = current.origin - ( bounds[0][2] + 1.0f ) * gravityNormal;
	contents = gameLocal.clip.Contents( point, NULL, mat3_identity, -1, self );
	if ( contents & MASK_WATER ) {

		waterType = contents;
		waterLevel = WATERLEVEL_FEET;

		// check at waist level
		point = current.origin - ( bounds[1][2] - bounds[0][2] ) * 0.5f * gravityNormal;
		contents = gameLocal.clip.Contents( point, NULL, mat3_identity, -1, self );
		if ( contents & MASK_WATER ) {

			waterLevel = WATERLEVEL_WAIST;

			// check at head level
			point = current.origin - ( bounds[1][2] - 1.0f ) * gravityNormal;
			contents = gameLocal.clip.Contents( point, NULL, mat3_identity, -1, self );
			if ( contents & MASK_WATER ) {
				waterLevel = WATERLEVEL_HEAD;
			}
		}
	}
}

/*
================
idPhysics_Player::DropTimers
================
*/
void idPhysics_Player::DropTimers( void ) {
	// drop misc timing counter
	if ( current.movementTime ) {
		if ( framemsec >= current.movementTime ) {
			current.movementFlags &= ~PMF_ALL_TIMES;
			current.movementTime = 0;
		}
		else {
			current.movementTime -= framemsec;
		}
	}
}

/*
================
idPhysics_Player::MovePlayer
================
*/
void idPhysics_Player::MovePlayer( int msec ) {

	// this counter lets us debug movement problems with a journal
	// by setting a conditional breakpoint for the previous frame
	c_pmove++;

	walking = false;
	groundPlane = false;
	ladder = false;

	// determine the time
	framemsec = msec;
	frametime = framemsec * 0.001f;

	// default speed
	playerSpeed = walkSpeed;

	// remove jumped and stepped up flag
	current.movementFlags &= ~(PMF_JUMPED|PMF_STEPPED_UP|PMF_STEPPED_DOWN);
	current.stepUp = 0.0f;

	if ( command.upmove < 10 ) {
		// not holding jump
		current.movementFlags &= ~PMF_JUMP_HELD;

		if ( m_mantlePhase == notMantling || m_mantlePhase == fixTheClipping )
				{
					// greebo: Jump button is released and no mantle phase is active,
					// we can allow the next mantling process.
					m_mantleStartPossible = true;
				}
	}

	// if no movement at all
	if ( current.movementType == PM_FREEZE ) {
		return;
	}

	// move the player velocity into the frame of a pusher
	current.velocity -= current.pushVelocity;

	// view vectors
	viewAngles.ToVectors( &viewForward, NULL, NULL );
	viewForward *= clipModelAxis;
	viewRight = gravityNormal.Cross( viewForward );
	viewRight.Normalize();

	// fly in spectator mode
	if ( current.movementType == PM_SPECTATOR ) {
		SpectatorMove();
		idPhysics_Player::DropTimers();
		return;
	}

	// special no clip mode
	if ( current.movementType == PM_NOCLIP ) {
		idPhysics_Player::NoclipMove();
		idPhysics_Player::DropTimers();
		return;
	}

	// no control when dead
	if ( current.movementType == PM_DEAD ) {
		command.forwardmove = 0;
		command.rightmove = 0;
		command.upmove = 0;
	}

	// set watertype and waterlevel
	idPhysics_Player::SetWaterLevel();

	// check for ground
	idPhysics_Player::CheckGround();

	// check if up against a ladder
	idPhysics_Player::CheckLadder();

	// set clip model size
	idPhysics_Player::CheckDuck();

	// handle timers
	idPhysics_Player::DropTimers();

	// Mantle Mod: SophisticatdZombie (DH)
	idPhysics_Player::UpdateMantleTimers();

	// Check if holding down jump
	if (CheckJumpHeldDown())
	{
		idPhysics_Player::PerformMantle();
	}

	// move
	if ( current.movementType == PM_DEAD ) {
		// dead
		idPhysics_Player::DeadMove();
	}
	// Mantle MOD
	// SophisticatedZombie (DH)
	// greebo: Do the MantleMove before checking the rope contacts
	else if ( !(m_mantlePhase == notMantling || m_mantlePhase == fixTheClipping) )
	{
		idPhysics_Player::MantleMove();
	}
	else if ( ladder ) {
		// going up or down a ladder
		idPhysics_Player::LadderMove();
	}
	else if ( current.movementFlags & PMF_TIME_WATERJUMP ) {
		// jumping out of water
		idPhysics_Player::WaterJumpMove();
	}
	else if ( waterLevel > 1 ) {
		// swimming
		idPhysics_Player::WaterMove();
	}
	else if ( walking ) {
		// walking on ground
		idPhysics_Player::WalkMove();
	}
	else {
		// airborne
		idPhysics_Player::AirMove();
	}

	// set watertype, waterlevel and groundentity
	idPhysics_Player::SetWaterLevel();
	idPhysics_Player::CheckGround();

	// move the player velocity back into the world frame
	current.velocity += current.pushVelocity;
	current.pushVelocity.Zero();
}

/*
================
idPhysics_Player::GetWaterLevel
================
*/
waterLevel_t idPhysics_Player::GetWaterLevel( void ) const {
	return waterLevel;
}

/*
================
idPhysics_Player::GetWaterType
================
*/
int idPhysics_Player::GetWaterType( void ) const {
	return waterType;
}

/*
================
idPhysics_Player::HasJumped
================
*/
bool idPhysics_Player::HasJumped( void ) const {
	return ( ( current.movementFlags & PMF_JUMPED ) != 0 );
}

/*
================
idPhysics_Player::HasSteppedUp
================
*/
bool idPhysics_Player::HasSteppedUp( void ) const {
	return ( ( current.movementFlags & ( PMF_STEPPED_UP | PMF_STEPPED_DOWN ) ) != 0 );
}

/*
================
idPhysics_Player::GetStepUp
================
*/
float idPhysics_Player::GetStepUp( void ) const {
	return current.stepUp;
}

/*
================
idPhysics_Player::IsCrouching
================
*/
bool idPhysics_Player::IsCrouching( void ) const {
	return ( ( current.movementFlags & PMF_DUCKED ) != 0 );
}

/*
================
idPhysics_Player::OnLadder
================
*/
bool idPhysics_Player::OnLadder( void ) const {
	return ladder;
}

/*
================
idPhysics_Player::idPhysics_Player
================
*/
idPhysics_Player::idPhysics_Player( void ) {
	debugLevel = false;
	clipModel = NULL;
	clipMask = 0;
	memset( &current, 0, sizeof( current ) );
	saved = current;
	walkSpeed = 0;
	crouchSpeed = 0;
	maxStepHeight = 0;
	maxJumpHeight = 0;
	memset( &command, 0, sizeof( command ) );
	viewAngles.Zero();
	framemsec = 0;
	frametime = 0;
	playerSpeed = 0;
	viewForward.Zero();
	viewRight.Zero();
	walking = false;
	groundPlane = false;
	memset( &groundTrace, 0, sizeof( groundTrace ) );
	groundMaterial = NULL;
	ladder = false;
	ladderNormal.Zero();
	waterLevel = WATERLEVEL_NONE;
	waterType = 0;

	// Mantle Mod
	m_mantlePhase = notMantling;
	m_mantleTime = 0.0;
	m_p_mantledEntity = NULL;
	m_mantledEntityID = 0;
	m_jumpHeldDownTime = 0.0;
	m_mantleStartPossible = true;
}

/*
================
idPhysics_Player_SavePState
================
*/
void idPhysics_Player_SavePState( idSaveGame *savefile, const playerPState_t &state ) {
	savefile->WriteVec3( state.origin );
	savefile->WriteVec3( state.velocity );
	savefile->WriteVec3( state.localOrigin );
	savefile->WriteVec3( state.pushVelocity );
	savefile->WriteFloat( state.stepUp );
	savefile->WriteInt( state.movementType );
	savefile->WriteInt( state.movementFlags );
	savefile->WriteInt( state.movementTime );
}

/*
================
idPhysics_Player_RestorePState
================
*/
void idPhysics_Player_RestorePState( idRestoreGame *savefile, playerPState_t &state ) {
	savefile->ReadVec3( state.origin );
	savefile->ReadVec3( state.velocity );
	savefile->ReadVec3( state.localOrigin );
	savefile->ReadVec3( state.pushVelocity );
	savefile->ReadFloat( state.stepUp );
	savefile->ReadInt( state.movementType );
	savefile->ReadInt( state.movementFlags );
	savefile->ReadInt( state.movementTime );
}

/*
================
idPhysics_Player::Save
================
*/
void idPhysics_Player::Save( idSaveGame *savefile ) const {

	idPhysics_Player_SavePState( savefile, current );
	idPhysics_Player_SavePState( savefile, saved );

	savefile->WriteFloat( walkSpeed );
	savefile->WriteFloat( crouchSpeed );
	savefile->WriteFloat( maxStepHeight );
	savefile->WriteFloat( maxJumpHeight );
	savefile->WriteInt( debugLevel );

	savefile->WriteUsercmd( command );
	savefile->WriteAngles( viewAngles );

	savefile->WriteInt( framemsec );
	savefile->WriteFloat( frametime );
	savefile->WriteFloat( playerSpeed );
	savefile->WriteVec3( viewForward );
	savefile->WriteVec3( viewRight );

	savefile->WriteBool( walking );
	savefile->WriteBool( groundPlane );
	savefile->WriteTrace( groundTrace );
	savefile->WriteMaterial( groundMaterial );

	savefile->WriteBool( ladder );
	savefile->WriteVec3( ladderNormal );

	savefile->WriteInt( (int)waterLevel );
	savefile->WriteInt( waterType );

	// Mantle mod
	savefile->WriteInt(m_mantlePhase);
	savefile->WriteBool(m_mantleStartPossible);
	savefile->WriteVec3(m_mantlePullStartPos);
	savefile->WriteVec3(m_mantlePullEndPos);
	savefile->WriteVec3(m_mantlePushEndPos);
	savefile->WriteObject(m_p_mantledEntity);
	savefile->WriteInt(m_mantledEntityID);
	savefile->WriteFloat(m_mantleTime);
	savefile->WriteFloat(m_jumpHeldDownTime);
}

/*
================
idPhysics_Player::Restore
================
*/
void idPhysics_Player::Restore( idRestoreGame *savefile ) {

	idPhysics_Player_RestorePState( savefile, current );
	idPhysics_Player_RestorePState( savefile, saved );

	savefile->ReadFloat( walkSpeed );
	savefile->ReadFloat( crouchSpeed );
	savefile->ReadFloat( maxStepHeight );
	savefile->ReadFloat( maxJumpHeight );
	savefile->ReadInt( debugLevel );

	savefile->ReadUsercmd( command );
	savefile->ReadAngles( viewAngles );

	savefile->ReadInt( framemsec );
	savefile->ReadFloat( frametime );
	savefile->ReadFloat( playerSpeed );
	savefile->ReadVec3( viewForward );
	savefile->ReadVec3( viewRight );

	savefile->ReadBool( walking );
	savefile->ReadBool( groundPlane );
	savefile->ReadTrace( groundTrace );
	savefile->ReadMaterial( groundMaterial );

	savefile->ReadBool( ladder );
	savefile->ReadVec3( ladderNormal );

	savefile->ReadInt( (int &)waterLevel );
	savefile->ReadInt( waterType );

	// Mantle mod
	int temp;
	savefile->ReadInt(temp);
	assert(temp >= 0 && temp < NumMantlePhases); // sanity check
	m_mantlePhase = static_cast<EMantlePhase>(temp);

	savefile->ReadBool(m_mantleStartPossible);
	savefile->ReadVec3(m_mantlePullStartPos);
	savefile->ReadVec3(m_mantlePullEndPos);
	savefile->ReadVec3(m_mantlePushEndPos);
	savefile->ReadObject(reinterpret_cast<idClass*&>(m_p_mantledEntity));
	savefile->ReadInt(m_mantledEntityID);
	savefile->ReadFloat(m_mantleTime);
	savefile->ReadFloat(m_jumpHeldDownTime);
}

/*
================
idPhysics_Player::SetPlayerInput
================
*/
void idPhysics_Player::SetPlayerInput( const usercmd_t &cmd, const idAngles &newViewAngles ) {
	command = cmd;
	viewAngles = newViewAngles;		// can't use cmd.angles cause of the delta_angles
}

/*
================
idPhysics_Player::SetSpeed
================
*/
void idPhysics_Player::SetSpeed( const float newWalkSpeed, const float newCrouchSpeed ) {
	walkSpeed = newWalkSpeed;
	crouchSpeed = newCrouchSpeed;
}

/*
================
idPhysics_Player::SetMaxStepHeight
================
*/
void idPhysics_Player::SetMaxStepHeight( const float newMaxStepHeight ) {
	maxStepHeight = newMaxStepHeight;
}

/*
================
idPhysics_Player::GetMaxStepHeight
================
*/
float idPhysics_Player::GetMaxStepHeight( void ) const {
	return maxStepHeight;
}

/*
================
idPhysics_Player::SetMaxJumpHeight
================
*/
void idPhysics_Player::SetMaxJumpHeight( const float newMaxJumpHeight ) {
	maxJumpHeight = newMaxJumpHeight;
}

/*
================
idPhysics_Player::SetMovementType
================
*/
void idPhysics_Player::SetMovementType( const pmtype_t type ) {
	current.movementType = type;
}

/*
================
idPhysics_Player::SetKnockBack
================
*/
void idPhysics_Player::SetKnockBack( const int knockBackTime ) {
	if ( current.movementTime ) {
		return;
	}
	current.movementFlags |= PMF_TIME_KNOCKBACK;
	current.movementTime = knockBackTime;
}

/*
================
idPhysics_Player::SetDebugLevel
================
*/
void idPhysics_Player::SetDebugLevel( bool set ) {
	debugLevel = set;
}

/*
================
idPhysics_Player::Evaluate
================
*/
bool idPhysics_Player::Evaluate( int timeStepMSec, int endTimeMSec ) {
	idVec3 masterOrigin, oldOrigin;
	idMat3 masterAxis;

	waterLevel = WATERLEVEL_NONE;
	waterType = 0;
	oldOrigin = current.origin;

	clipModel->Unlink();

	// if bound to a master
	if ( masterEntity ) {
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.origin = masterOrigin + current.localOrigin * masterAxis;
		clipModel->Link( gameLocal.clip, self, 0, current.origin, clipModel->GetAxis() );
		current.velocity = ( current.origin - oldOrigin ) / ( timeStepMSec * 0.001f );
		masterDeltaYaw = masterYaw;
		masterYaw = masterAxis[0].ToYaw();
		masterDeltaYaw = masterYaw - masterDeltaYaw;
		return true;
	}

	ActivateContactEntities();

	idPhysics_Player::MovePlayer( timeStepMSec );

	clipModel->Link( gameLocal.clip, self, 0, current.origin, clipModel->GetAxis() );

	if ( IsOutsideWorld() ) {
		gameLocal.Warning( "clip model outside world bounds for entity '%s' at (%s)", self->name.c_str(), current.origin.ToString(0) );
	}

	return true; //( current.origin != oldOrigin );
}

/*
================
idPhysics_Player::UpdateTime
================
*/
void idPhysics_Player::UpdateTime( int endTimeMSec ) {
}

/*
================
idPhysics_Player::GetTime
================
*/
int idPhysics_Player::GetTime( void ) const {
	return gameLocal.time;
}

/*
================
idPhysics_Player::GetImpactInfo
================
*/
void idPhysics_Player::GetImpactInfo( const int id, const idVec3 &point, impactInfo_t *info ) const {
	info->invMass = invMass;
	info->invInertiaTensor.Zero();
	info->position.Zero();
	info->velocity = current.velocity;
}

/*
================
idPhysics_Player::ApplyImpulse
================
*/
void idPhysics_Player::ApplyImpulse( const int id, const idVec3 &point, const idVec3 &impulse ) {
	if ( current.movementType != PM_NOCLIP ) {
		current.velocity += impulse * invMass;
	}
}

/*
================
idPhysics_Player::IsAtRest
================
*/
bool idPhysics_Player::IsAtRest( void ) const {
	return false;
}

/*
================
idPhysics_Player::GetRestStartTime
================
*/
int idPhysics_Player::GetRestStartTime( void ) const {
	return -1;
}

/*
================
idPhysics_Player::SaveState
================
*/
void idPhysics_Player::SaveState( void ) {
	saved = current;
}

/*
================
idPhysics_Player::RestoreState
================
*/
void idPhysics_Player::RestoreState( void ) {
	current = saved;

	clipModel->Link( gameLocal.clip, self, 0, current.origin, clipModel->GetAxis() );

	EvaluateContacts();
}

/*
================
idPhysics_Player::SetOrigin
================
*/
void idPhysics_Player::SetOrigin( const idVec3 &newOrigin, int id ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	current.localOrigin = newOrigin;
	if ( masterEntity ) {
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.origin = masterOrigin + newOrigin * masterAxis;
	}
	else {
		current.origin = newOrigin;
	}

	clipModel->Link( gameLocal.clip, self, 0, newOrigin, clipModel->GetAxis() );
}

/*
================
idPhysics_Player::GetOrigin
================
*/
const idVec3 & idPhysics_Player::PlayerGetOrigin( void ) const {
	return current.origin;
}

/*
================
idPhysics_Player::SetAxis
================
*/
void idPhysics_Player::SetAxis( const idMat3 &newAxis, int id ) {
	clipModel->Link( gameLocal.clip, self, 0, clipModel->GetOrigin(), newAxis );
}

/*
================
idPhysics_Player::Translate
================
*/
void idPhysics_Player::Translate( const idVec3 &translation, int id ) {

	current.localOrigin += translation;
	current.origin += translation;

	clipModel->Link( gameLocal.clip, self, 0, current.origin, clipModel->GetAxis() );
}

/*
================
idPhysics_Player::Rotate
================
*/
void idPhysics_Player::Rotate( const idRotation &rotation, int id ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	current.origin *= rotation;
	if ( masterEntity ) {
		self->GetMasterPosition( masterOrigin, masterAxis );
		current.localOrigin = ( current.origin - masterOrigin ) * masterAxis.Transpose();
	}
	else {
		current.localOrigin = current.origin;
	}

	clipModel->Link( gameLocal.clip, self, 0, current.origin, clipModel->GetAxis() * rotation.ToMat3() );
}

/*
================
idPhysics_Player::SetLinearVelocity
================
*/
void idPhysics_Player::SetLinearVelocity( const idVec3 &newLinearVelocity, int id ) {
	current.velocity = newLinearVelocity;
}

/*
================
idPhysics_Player::GetLinearVelocity
================
*/
const idVec3 &idPhysics_Player::GetLinearVelocity( int id ) const {
	return current.velocity;
}

/*
================
idPhysics_Player::SetPushed
================
*/
void idPhysics_Player::SetPushed( int deltaTime ) {
	idVec3 velocity;
	float d;

	// velocity with which the player is pushed
	velocity = ( current.origin - saved.origin ) / ( deltaTime * idMath::M_MS2SEC );

	// remove any downward push velocity
	d = velocity * gravityNormal;
	if ( d > 0.0f ) {
		velocity -= d * gravityNormal;
	}

	current.pushVelocity += velocity;
}

/*
================
idPhysics_Player::GetPushedLinearVelocity
================
*/
const idVec3 &idPhysics_Player::GetPushedLinearVelocity( const int id ) const {
	return current.pushVelocity;
}

/*
================
idPhysics_Player::ClearPushedVelocity
================
*/
void idPhysics_Player::ClearPushedVelocity( void ) {
	current.pushVelocity.Zero();
}

/*
================
idPhysics_Player::SetMaster

  the binding is never orientated
================
*/
void idPhysics_Player::SetMaster( idEntity *master, const bool orientated ) {
	idVec3 masterOrigin;
	idMat3 masterAxis;

	if ( master ) {
		if ( !masterEntity ) {
			// transform from world space to master space
			self->GetMasterPosition( masterOrigin, masterAxis );
			current.localOrigin = ( current.origin - masterOrigin ) * masterAxis.Transpose();
			masterEntity = master;
			masterYaw = masterAxis[0].ToYaw();
		}
		ClearContacts();
	}
	else {
		if ( masterEntity ) {
			masterEntity = NULL;
		}
	}
}

const float	PLAYER_VELOCITY_MAX				= 4000;
const int	PLAYER_VELOCITY_TOTAL_BITS		= 16;
const int	PLAYER_VELOCITY_EXPONENT_BITS	= idMath::BitsForInteger( idMath::BitsForFloat( PLAYER_VELOCITY_MAX ) ) + 1;
const int	PLAYER_VELOCITY_MANTISSA_BITS	= PLAYER_VELOCITY_TOTAL_BITS - 1 - PLAYER_VELOCITY_EXPONENT_BITS;
const int	PLAYER_MOVEMENT_TYPE_BITS		= 3;
const int	PLAYER_MOVEMENT_FLAGS_BITS		= 8;

/*
================
idPhysics_Player::WriteToSnapshot
================
*/
void idPhysics_Player::WriteToSnapshot( idBitMsgDelta &msg ) const {
	msg.WriteFloat( current.origin[0] );
	msg.WriteFloat( current.origin[1] );
	msg.WriteFloat( current.origin[2] );
	msg.WriteFloat( current.velocity[0], PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	msg.WriteFloat( current.velocity[1], PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	msg.WriteFloat( current.velocity[2], PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( current.origin[0], current.localOrigin[0] );
	msg.WriteDeltaFloat( current.origin[1], current.localOrigin[1] );
	msg.WriteDeltaFloat( current.origin[2], current.localOrigin[2] );
	msg.WriteDeltaFloat( 0.0f, current.pushVelocity[0], PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.pushVelocity[1], PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.pushVelocity[2], PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	msg.WriteDeltaFloat( 0.0f, current.stepUp );
	msg.WriteBits( current.movementType, PLAYER_MOVEMENT_TYPE_BITS );
	msg.WriteBits( current.movementFlags, PLAYER_MOVEMENT_FLAGS_BITS );
	msg.WriteDeltaInt( 0, current.movementTime );
}

/*
================
idPhysics_Player::ReadFromSnapshot
================
*/
void idPhysics_Player::ReadFromSnapshot( const idBitMsgDelta &msg ) {
	current.origin[0] = msg.ReadFloat();
	current.origin[1] = msg.ReadFloat();
	current.origin[2] = msg.ReadFloat();
	current.velocity[0] = msg.ReadFloat( PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	current.velocity[1] = msg.ReadFloat( PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	current.velocity[2] = msg.ReadFloat( PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	current.localOrigin[0] = msg.ReadDeltaFloat( current.origin[0] );
	current.localOrigin[1] = msg.ReadDeltaFloat( current.origin[1] );
	current.localOrigin[2] = msg.ReadDeltaFloat( current.origin[2] );
	current.pushVelocity[0] = msg.ReadDeltaFloat( 0.0f, PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	current.pushVelocity[1] = msg.ReadDeltaFloat( 0.0f, PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	current.pushVelocity[2] = msg.ReadDeltaFloat( 0.0f, PLAYER_VELOCITY_EXPONENT_BITS, PLAYER_VELOCITY_MANTISSA_BITS );
	current.stepUp = msg.ReadDeltaFloat( 0.0f );
	current.movementType = msg.ReadBits( PLAYER_MOVEMENT_TYPE_BITS );
	current.movementFlags = msg.ReadBits( PLAYER_MOVEMENT_FLAGS_BITS );
	current.movementTime = msg.ReadDeltaInt( 0 );

	if ( clipModel ) {
		clipModel->Link( gameLocal.clip, self, 0, current.origin, clipModel->GetAxis() );
	}
}

/*
================
idPhysics_Player::PerformMantle
================
*/
void idPhysics_Player::PerformMantle( void ) {

	// Can't start mantle if already mantling or not yet possible (jump button not yet released)
	if ( !(m_mantlePhase == notMantling || m_mantlePhase == fixTheClipping ) || !m_mantleStartPossible ) {
		return;
	}

	/* no imobilitzation system on our end
	if (static_cast<idPlayer*>(self)->GetImmobilization() & EIM_MANTLE)
	{
		return; // greebo: Mantling disabled by immobilization system
	}
	*/
	//p_player->SetInfluenceLevel( INFLUENCE_LEVEL3 );
	//p_player->LowerWeapon();

	// Clear mantled entity members to indicate nothing is
	// being mantled
	m_p_mantledEntity = NULL;
	m_mantledEntityID = 0;

	// Forward vector is direction player is looking
	idVec3 forward = viewAngles.ToForward();
	forward.Normalize();

	// We use gravity a lot here...
	idVec3 gravityNormal = GetGravityNormal();
	idVec3 upVector = -gravityNormal;

	// Get maximum reach distances for mantling
	float maxVerticalReachDistance;
	float maxHorizontalReachDistance;
	float maxMantleTraceDistance;

	GetCurrentMantlingReachDistances
	(
		maxVerticalReachDistance,
		maxHorizontalReachDistance,
		maxMantleTraceDistance
	);


	// Get start position of gaze trace, which is player's eye position
	idPlayer* p_player = static_cast<idPlayer*>(self);

	if (p_player == NULL)
	{
		/* we don't have a logging system on our end
		DM_LOG(LC_MOVEMENT, LT_ERROR)LOGSTRING("p_player is NULL\r");
		*/
		return;
	}
	/* we don't have a log system on our end
	DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("Getting eye position\r");
	*/
	idVec3 eyePos = p_player->GetEyePosition();

	/* we're not holding objects on our end
	// Ishtvan: Do not attempt to mantle if holding an object
	if( gameLocal.m_Grabber->GetSelected() )
		return;
	*/

	// Run mantle trace
	trace_t trace;

	MantleTargetTrace
	(
		maxMantleTraceDistance,
		eyePos,
		forward,
		trace
	);

	// If the trace found a target, see if it is mantleable
	if ( trace.fraction < 1.0f )
	{
		// mantle target found
		/*we don't hold a log on our end
		// Log trace hit point
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING
		(
			"Mantle target trace collision point (%f %f %f)\r",
			trace.c.point.x,
			trace.c.point.y,
			trace.c.point.z
		);
		*/
		// Find mantle end point and make sure mantle is
		// possible
		idVec3 mantleEndPoint;
		if (ComputeMantlePathForTarget ( maxVerticalReachDistance, maxHorizontalReachDistance, eyePos, trace, mantleEndPoint )) {
			// Mantle target passed mantleability tests
			/* we don't hold any logs on our end
			// Log the end point
			DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING
			(
				"Mantle end position = (%f %f %f)\r",
				mantleEndPoint.x,
				mantleEndPoint.y,
				mantleEndPoint.z
			);
			*/
			// Start with log phase dependent on position relative
			// to the mantle end point
			if (mantleEndPoint * gravityNormal < eyePos * gravityNormal)
			{
				// Start with pull if on the ground, hang if not
				if (groundPlane)
				{
					StartMantle(mantlingPulling, eyePos, GetOrigin(), mantleEndPoint);
				}
				else
				{
					StartMantle(mantlingHanging, eyePos, GetOrigin(), mantleEndPoint);
				}
			}
			else
			{
				// We are above it, start with push
				StartMantle(mantlingPushing, eyePos, GetOrigin(), mantleEndPoint);
			}
		}
	}
}

/*
================
idPhysics_Player::GetCurrentMantlingReachDistances
================
*/
void idPhysics_Player::GetCurrentMantlingReachDistances ( float& out_maxVerticalReachDistance,
		                                                  float& out_maxHorizontalReachDistance,
														  float& out_maxMantleTraceDistance
														) {
	// Determine arm reach in each direction
	float armReach = pm_normalheight.GetFloat() * pm_mantle_reach.GetFloat();
	float armVerticalReach = pm_normalheight.GetFloat() * pm_mantle_height.GetFloat();

	// Trace out as far as horizontal arm length from player
	out_maxMantleTraceDistance = armReach;

	// Determine maximum vertical and horizontal distance components for
	// a mantleable surface
	if (current.movementFlags & PMF_DUCKED /*&& !OnRope() && !OnLadder()*/)
	{
		out_maxVerticalReachDistance = pm_crouchheight.GetFloat() + armVerticalReach;
	}
	/*else if (OnRope() || OnLadder())
	{
		// angua: need larger reach when on rope
		out_maxVerticalReachDistance = pm_normalheight.GetFloat() + armVerticalReach;
		out_maxHorizontalReachDistance = 2* armReach;
		out_maxMantleTraceDistance *= 2;
	}*/
	else
	{
		// This vertical distance is up from the players feet
		out_maxVerticalReachDistance = pm_normalheight.GetFloat() + armVerticalReach;
	}
	out_maxHorizontalReachDistance = armReach;
}

/*
================
idPhysics_Player::MantleTargetTrace
================
*/
void idPhysics_Player::MantleTargetTrace ( float maxMantleTraceDistance,
										   const idVec3& eyePos,
										   const idVec3& forwardVec,
										   trace_t& out_trace
										 ) {
	// Calculate end point of gaze trace
	idVec3 end = eyePos + (maxMantleTraceDistance * forwardVec);

	// Run gaze trace
	gameLocal.clip.TracePoint( out_trace, eyePos, end, MASK_SOLID, self );

	// If that trace didn't hit anything, try a taller trace forward along the midline
	// of the player's body for the full player's height out the trace distance.
	if ( out_trace.fraction >= 1.0f )
	{
		idVec3 upVector = -GetGravityNormal();

		// Project forward vector onto the a plane perpendicular to gravity
		idVec3 forwardPerpGrav = forwardVec;
		forwardPerpGrav.ProjectOntoPlane(upVector);

		// Create bounds for translation trace model
		idBounds bounds = clipModel->GetBounds();
		idBounds savedBounds = bounds;

		bounds[0][1] = (savedBounds[0][1] + savedBounds[1][1]) / 2;
		bounds[0][1] -= 0.01f;
		bounds[1][1] = bounds[0][1] + 0.02f;
		bounds[0][0] = bounds[0][1];
		bounds[1][0] = bounds[1][1];

		clipModel->LoadModel( pm_usecylinder.GetBool() ? idTraceModel(bounds, 8) : idTraceModel(bounds) );
		/* we don't sport a log system on our end
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantle gaze trace didn't hit anything, so doing forward movement trace for mantle target\r");
		*/
		end = current.origin + (maxMantleTraceDistance * forwardPerpGrav);
		gameLocal.clip.Translation ( out_trace, current.origin, end, clipModel, clipModel->GetAxis(), MASK_SOLID, self );

		//gameRenderWorld->DebugBounds(colorCyan, bounds, current.origin, 2000);
		//gameRenderWorld->DebugBounds(colorBlue, bounds, current.origin + (maxMantleTraceDistance * forwardPerpGrav), 2000);

		// Restore player clip model to normal
		clipModel->LoadModel( pm_usecylinder.GetBool() ? idTraceModel(savedBounds, 8) : idTraceModel(savedBounds) );
	}

	// Get the entity to be mantled
	if (out_trace.c.entityNum != ENTITYNUM_NONE)
	{
		// Track entity which is was the chosen target
		m_p_mantledEntity = gameLocal.entities[out_trace.c.entityNum];

		if (m_p_mantledEntity->IsMantleable())
		{
			m_mantledEntityID = out_trace.c.id;
			/* no logging on our end
			DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantle target entity is called '%s'\r", m_p_mantledEntity->name.c_str());
			*/
		}
		else
		{
			// Oops, this entity isn't mantleable
			m_p_mantledEntity = NULL;
			out_trace.fraction = 1.0f; // Pretend we didn't hit anything
		}
	}
}
/*
================
idPhysics_Player::ComputeMantlePathForTarget
================
*/
bool idPhysics_Player::ComputeMantlePathForTarget (	float maxVerticalReachDistance,
												    float maxHorizontalReachDistance,
													const idVec3& eyePos,
													trace_t& in_targetTraceResult,
													idVec3& out_mantleEndPoint
												  ) {
	// Up vector
	idVec3 upVector = -GetGravityNormal();

	// Mantle start point is origin
	const idVec3& mantleStartPoint = GetOrigin();

	// Check if trace target has a mantleable surface
	bool b_canBeMantled = DetermineIfMantleTargetHasMantleableSurface ( maxVerticalReachDistance,
																		maxHorizontalReachDistance,
																		in_targetTraceResult,
																		out_mantleEndPoint
																	  );

	if (b_canBeMantled) {
		// Check if path to mantle end point is not blocked
		b_canBeMantled &= DetermineIfPathToMantleSurfaceIsPossible ( maxVerticalReachDistance,
																	 maxHorizontalReachDistance,
																	 eyePos,
																	 mantleStartPoint,
																	 out_mantleEndPoint
																   );

		if (b_canBeMantled)
		{
			// Is end point too far away?
			idVec3 endDistanceVector = out_mantleEndPoint - eyePos;
			float endDistance = endDistanceVector.Length();
			idVec3 upDistance = endDistanceVector;

			upDistance.x *= upVector.x;
			upDistance.y *= upVector.y;
			upDistance.z *= upVector.z;
			float upDist = upDistance.Length();

			float nonUpDist = idMath::Sqrt(endDistance*endDistance - upDist*upDist);

			// Check the calculated distances
			if (upDist < 0.0)
			{
				/*can't log on my end
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantleable surface was below player's feet. No belly slide allowed.\r");
				*/
				b_canBeMantled = false;
			}
			else if	(upDist > maxVerticalReachDistance || nonUpDist > maxHorizontalReachDistance)
			{
				// Its too far away either horizontally or vertically
				/*on my end there's not any means of logging
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING
				(
					"Distance to end point was (%f, %f) (horizontal, vertical) which is greater than limits of (%f %f), so mantle cannot be done\n",
					upDist,
					nonUpDist,
					maxVerticalReachDistance,
					maxHorizontalReachDistance
				);
				*/

				b_canBeMantled = false;
			}

			// Distances are reasonable
		}
	}

	// Return result
	return b_canBeMantled;
}

/*
================
idPhysics_Player::DetermineIfMantleTargetHasMantleableSurface
================
*/

bool idPhysics_Player::DetermineIfMantleTargetHasMantleableSurface ( float maxVerticalReachDistance,
		  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	 float maxHorizontalReachDistance,
																	 trace_t& in_targetTraceResult,
																	 idVec3& out_mantleEndPoint
																   ) {
	// Never mantle onto non-mantleable entities (early exit)
	if (in_targetTraceResult.fraction < 1.0f)
	{
		idEntity* ent = gameLocal.entities[in_targetTraceResult.c.entityNum];

		if (ent == NULL || !ent->IsMantleable())
		{
			// The mantle target is an unmantleable entity
			return false;
		}
	}

	// Try moving player's bounding box up from the trace hit point
	// in steps up to the maximum distance and see if at any point
	// there are no collisions. If so, we can mantle.

	// First point to test has gravity orthogonal coordinates set
	// to the ray trace collision point. It then has gravity non-orthogonal
	// coordinates set from the current player origin.  However,
	// for the non-orthogonal-to-gravity coordinates, the trace.c.point
	// location is a better starting place.  Because of rear surface occlusion,
	// it will always be closer to the actual "upper" surface than the player
	// origin unless the object is "below" the player relative to gravity.
	// And, in that "below" case, mantling isn't possible anyway.

	// This sets coordinates to their components which are orthogonal
	// to gravity.
	idVec3 componentOrthogonalToGravity = in_targetTraceResult.c.point;
	componentOrthogonalToGravity.ProjectOntoPlane(-gravityNormal);

	// This sets coordintes to their components parallel to gravity
	idVec3 componentParallelToGravity;
	componentParallelToGravity.x = -gravityNormal.x * in_targetTraceResult.c.point.x;
	componentParallelToGravity.y = -gravityNormal.y * in_targetTraceResult.c.point.y;
	componentParallelToGravity.z = -gravityNormal.z * in_targetTraceResult.c.point.z;

	// What parallel to gravity reach distance is already used up at this point
	idVec3 originParallelToGravity;
	originParallelToGravity.x = -gravityNormal.x * current.origin.x;
	originParallelToGravity.y = -gravityNormal.y * current.origin.y;
	originParallelToGravity.z = -gravityNormal.z * current.origin.z;

	float verticalReachDistanceUsed = (componentParallelToGravity - originParallelToGravity).Length();

	/* no logging system on my end
	DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING
	(
		"Initial vertical reach distance used = %f out of maximum of %f\r",
		verticalReachDistanceUsed,
		maxVerticalReachDistance
	);
	*/

	// The first test point
	idVec3 testPosition = componentOrthogonalToGravity + componentParallelToGravity;

	// Load crouch model
	// as mantling ends in a crouch
	if (!(current.movementFlags & PMF_DUCKED))
	{
		idBounds bounds = clipModel->GetBounds();
		bounds[1][2] = pm_crouchheight.GetFloat();

		clipModel->LoadModel( pm_usecylinder.GetBool() ? idTraceModel(bounds, 8) : idTraceModel(bounds) );
	}

	// We try moving it up by the step distance up to the maximum height until
	// there are no collisions
	bool b_keepTesting = verticalReachDistanceUsed < maxVerticalReachDistance;
	bool b_mantlePossible = false;
	bool b_lastCollisionWasMantleable = true;

	trace_t worldMantleTrace;

	while (b_keepTesting)
	{
		// Try collision in_targetTraceResult
		idVec3 mantleTraceStart = testPosition;
		gameLocal.clip.Translation( worldMantleTrace, mantleTraceStart, testPosition, clipModel, clipModel->GetAxis(), clipMask, self );

		if (worldMantleTrace.fraction >= 1.0f)
		{
			// We can mantle to there, unless the last test collided with something non-mantleable.
			// Either way we're done here.
			b_keepTesting = false;

			if (b_lastCollisionWasMantleable)
			{
				b_mantlePossible = true;
			}
		}
		else
		{
			idEntity* ent = gameLocal.entities[ worldMantleTrace.c.entityNum ];

			if (ent && !ent->IsMantleable())
			{
				// If we collided with a non-mantleable entity, then flag that.
				// This is to prevent situations where we start out mantling on a low ledge
				// (like a stair) on which a non-mantleable entity (like an AI) is standing,
				// and proceed to mantle over the AI.
				b_lastCollisionWasMantleable = false;
			}
			else
			{
				// On the other hand, if there's a shelf above the AI, then we can still mantle
				// the shelf.
				b_lastCollisionWasMantleable = true;
			}

			if (verticalReachDistanceUsed < maxVerticalReachDistance)
			{
				// Try next test position

				float testIncrementAmount = maxVerticalReachDistance - verticalReachDistanceUsed;

				// Establish upper bound for increment test size
				if (testIncrementAmount > MANTLE_TEST_INCREMENT)
				{
					testIncrementAmount = MANTLE_TEST_INCREMENT;
				}

				// Establish absolute minimum increment size so that
				// we don't approach increment size below floating point precision,
				// which would cause an infinite loop.
				if (testIncrementAmount < 1.0f)
				{
					testIncrementAmount = 1.0f;
				}

				// Update location by increment size
				componentParallelToGravity += (-gravityNormal * testIncrementAmount);
				verticalReachDistanceUsed = (componentParallelToGravity - originParallelToGravity).Length();
				/* logging not posible in my end
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING
				(
					"Ledge Search: Vertical reach distance used = %f out of maximum of %f\r",
					verticalReachDistanceUsed,
					maxVerticalReachDistance
				);
				*/
				// Modify test position
				testPosition = componentOrthogonalToGravity + componentParallelToGravity;
			}
			else
			{
				// No surface we could fit on against gravity from raytrace hit point
				// up as far as we can reach
				b_keepTesting = false;
				/* there isn't a logging system on my end
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("No mantleable surface within reach distance\r");
				*/
			}
		}

	}

	// Don't mantle onto surfaces that are too steep.
	// Any surface with an angle whose cosine is
	// smaller than MIN_FLATNESS is too steep.
	float minFlatness = pm_mantle_minflatness.GetFloat();

	if (b_mantlePossible)
	{
		// Attempt to get the normal of the surface we'd be standing on
		// In rare cases this may not collide
		trace_t floorTrace;
		gameLocal.clip.Translation( floorTrace, testPosition,
			testPosition + (gravityNormal * MANTLE_TEST_INCREMENT),
			clipModel, clipModel->GetAxis(), clipMask, self );

		if (floorTrace.fraction < 1.0f)
		{
			// Uses the dot product to compare against the cosine of an angle.
			// Comparing to cos(90)=0 means we can mantle on top of any surface
			// Comparing to cos(0)=1 means we can only mantle on top of perfectly flat surfaces

			float flatness = floorTrace.c.normal * (-gravityNormal);

			/* no log on my end
			DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING(
				"Floor %.2f,%.2f,%.2f; grav %.2f,%.2f,%.2f; dot %f; %s\r",
				floorTrace.c.normal.x, floorTrace.c.normal.y, floorTrace.c.normal.z,
				gravityNormal.x, gravityNormal.y, gravityNormal.z,
				flatness, flatness < minFlatness ? "too steep" : "OK");
			*/
			if (flatness < minFlatness)
			{
				b_mantlePossible = false;
			}
		}
	}

	// Must restore standing model if player is not crouched
	if (!(current.movementFlags & PMF_DUCKED))
	{
		// Load back standing model
		idBounds bounds = clipModel->GetBounds();
		bounds[1][2] = pm_normalheight.GetFloat();

		clipModel->LoadModel( pm_usecylinder.GetBool() ? idTraceModel(bounds, 8) : idTraceModel(bounds) );
	}

	// Return result
	if (b_mantlePossible)
	{
		out_mantleEndPoint = testPosition;
	}
	return b_mantlePossible;
}

/*
================
idPhysics_Player::DetermineIfPathToMantleSurfaceIsPossible
================
*/
bool idPhysics_Player::DetermineIfPathToMantleSurfaceIsPossible ( float maxVerticalReachDistance,
																  float maxHorizontalReachDistance,
																  const idVec3& in_eyePos,
																  const idVec3& in_mantleStartPoint,
																  const idVec3& in_mantleEndPoint
																) {
	// Make sure path from current location
	// upward can be traversed.
	trace_t roomForMoveUpTrace;
	idVec3 MoveUpStart = in_mantleStartPoint;
	idVec3 MoveUpEnd;

	// Go to coordinate components against gravity from current location
	idVec3 componentOrthogonalToGravity;
	componentOrthogonalToGravity = in_mantleStartPoint;
	componentOrthogonalToGravity.ProjectOntoPlane (-gravityNormal);
	MoveUpEnd = componentOrthogonalToGravity;

	MoveUpEnd.x += -gravityNormal.x * in_mantleEndPoint.x;
	MoveUpEnd.y += -gravityNormal.y * in_mantleEndPoint.y;
	MoveUpEnd.z += -gravityNormal.z * in_mantleEndPoint.z;

	// Use crouch clip model
	if (!(current.movementFlags & PMF_DUCKED))
	{
		// Load crouching model
		idBounds bounds = clipModel->GetBounds();
		bounds[1][2] = pm_crouchheight.GetFloat();

		clipModel->LoadModel( pm_usecylinder.GetBool() ? idTraceModel(bounds, 8) : idTraceModel(bounds) );
	}

	gameLocal.clip.Translation
	(
		roomForMoveUpTrace,
		MoveUpStart,
		MoveUpEnd,
		clipModel,
		clipModel->GetAxis(),
		clipMask,
		self
	);

	// Done with crouch model if not currently crouched
	if (!(current.movementFlags & PMF_DUCKED))
	{
		// Load back standing model
		idBounds bounds = clipModel->GetBounds();
		bounds[1][2] = pm_normalheight.GetFloat();

		clipModel->LoadModel( pm_usecylinder.GetBool() ? idTraceModel(bounds, 8) : idTraceModel(bounds) );
	}

	// Log
	if (roomForMoveUpTrace.fraction < 1.0)
	{
		/* I DON'T HAVE A LO IN MY SYSTEM
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING
		(
			"Collision test from (%f %f %f) to (%f %f %f) yieled trace fraction %f\r",
			MoveUpStart.x,
			MoveUpStart.y,
			MoveUpStart.z,
			MoveUpEnd.x,
			MoveUpEnd.y,
			MoveUpEnd.z,
			roomForMoveUpTrace.fraction
		);

		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("Not enough vertical clearance along mantle path\r");
		*/
		return false;
	}
	else
	{
		return true;
	}
}

/*
================
idPhysics_Player::StartMantle
================
*/
void idPhysics_Player::StartMantle ( EMantlePhase initialMantlePhase, idVec3 eyePos, idVec3 startPos, idVec3 endPos ) {

	idPlayer* player = static_cast<idPlayer*>(self);

	// Ishtvan 10/16/05
	// If mantling starts while on a rope, detach from that rope
	/* no ropes on our game
	if ( m_bOnRope )
	{
		RopeDetach();
	}
	*/

	// If mantling starts while climbing a ladder, detach from climbing surface
	/* not using this method in order to gather if the player is on a ladder
	if ( m_bOnClimb )
	{
		ClimbDetach();
	}
	*/
	// Ishtvan 11/20/05 - Lower weapons when mantling
	/* TODO hide the weapon: maybe with a setInfluence?
	static_cast<idPlayer*>(self)->SetImmobilization( "MantleMove", EIM_WEAPON_SELECT | EIM_ATTACK );
	*/
	player->SetInfluenceLevel( INFLUENCE_LEVEL3 );
	player->LowerWeapon();
	// greebo: Disable the next mantle start here, this is set to TRUE again
	// when the jump key is released outside a mantle phase
	m_mantleStartPossible = false;

	// If mantling from a jump, cancel any velocity so that it does
	// not continue after the mantle is completed.
	current.velocity.Zero();

	// Calculate mantle distance
	idVec3 mantleDistanceVec = endPos - startPos;
//	float mantleDistance = mantleDistanceVec.Length();

	// Log starting phase
	if (initialMantlePhase == mantlingHanging)
	{
		/* no log on my end
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantle starting with hang\r");
		*/
		// Impart a force on mantled object?
		if (m_p_mantledEntity != NULL && self != NULL)
		{
			impactInfo_t info;
			m_p_mantledEntity->GetImpactInfo(self, m_mantledEntityID, endPos, &info);

			if (info.invMass != 0.0f)
			{
				m_p_mantledEntity->ActivatePhysics(self);
				m_p_mantledEntity->ApplyImpulse( self, m_mantledEntityID, endPos, current.velocity / ( info.invMass * 2.0f ) );
			}
		}
	}
	else if (initialMantlePhase == mantlingPulling)
	{
		/* no logging system on my end
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantle starting with pull upward\r");
		*/
		player->StartSound("snd_player_mantle_pull", SND_CHANNEL_VOICE, 0, false, NULL); // grayman #3010
	}
	else if (initialMantlePhase == mantlingShiftHands)
	{
		/* no logging system on my end
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantle starting with shift hands\r");
		*/
	}
	else if (initialMantlePhase == mantlingPushing)
	{
		// Go into crouch
		current.movementFlags |= PMF_DUCKED;

		// Start with push upward
		/* no log system on our end
		DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING("Mantle starting with push upward\r");
		*/
		player->StartSound("snd_player_mantle_push", SND_CHANNEL_VOICE, 0, false, NULL); // grayman #3010
	}

	m_mantlePhase = initialMantlePhase;
	m_mantleTime = GetMantleTimeForPhase(m_mantlePhase);

	// Make positions relative to entity
	if (m_p_mantledEntity != NULL)
	{
		idPhysics* p_physics = m_p_mantledEntity->GetPhysics();
		if (p_physics != NULL)
		{
			const idVec3& mantledEntityOrigin = p_physics->GetOrigin();
			const idMat3& mantledEntityAxis = p_physics->GetAxis();

			// ishtvan 1/3/2010: Incorporate entity rotation as well as translation
			/*
			startPos -= mantledEntityOrigin;
			eyePos -= mantledEntityOrigin;
			endPos -= mantledEntityOrigin;
			*/
			startPos = (startPos - mantledEntityOrigin) * mantledEntityAxis.Transpose();
			eyePos = (eyePos - mantledEntityOrigin) * mantledEntityAxis.Transpose();
			endPos = (endPos - mantledEntityOrigin) * mantledEntityAxis.Transpose();
		}
	}

	// Set end position
	m_mantlePushEndPos = endPos;

	if (initialMantlePhase == mantlingPulling || initialMantlePhase == mantlingHanging)
	{
		// Pull from start position up to about 2/3 of eye height
		m_mantlePullStartPos = startPos;
		m_mantlePullEndPos = eyePos;

		m_mantlePullEndPos += GetGravityNormal() * pm_normalheight.GetFloat() / 3.0f;
	}
	else
	{
		// Starting with push from current position
		m_mantlePullEndPos = startPos;
	}
}

/*
================
idPhysics_Player::GetMantleTimeForPhase
================
*/
float idPhysics_Player::GetMantleTimeForPhase ( EMantlePhase mantlePhase )
{
	// Current implementation uses constants
	switch ( mantlePhase )
	{
	case mantlingHanging:
		return pm_mantle_hang_msecs.GetFloat();

	case mantlingPulling:
		return pm_mantle_pull_msecs.GetFloat();

	case mantlingShiftHands:
		return pm_mantle_shift_hands_msecs.GetFloat();

	case mantlingPushing:
		return pm_mantle_push_msecs.GetFloat();

	default:
		return 0.0f;
	}
}

/*
================
idPhysics_Player::UpdateMantleTimers
================
*/
void idPhysics_Player::UpdateMantleTimers()
{
	// Frame seconds left
	float framemSecLeft = framemsec;

	// Update jump held down timer: This actually grows, not drops
	if (!( current.movementFlags & PMF_JUMP_HELD ) )
	{
		m_jumpHeldDownTime = 0;
	}
	else
	{
		m_jumpHeldDownTime += framemsec;
	}

	// Skip all this if done mantling
	if (m_mantlePhase != notMantling && m_mantlePhase != fixTheClipping)
	{
		idPlayer* player = static_cast<idPlayer*>(self); // grayman #3010
		// Handle expiring mantle phases
		while (framemSecLeft >= m_mantleTime && m_mantlePhase != notMantling)
		{
			framemSecLeft -= m_mantleTime;
			m_mantleTime = 0;

			// Advance mantle phase
			switch (m_mantlePhase)
			{
			case mantlingHanging:
				/* we don't have a logging system in our end
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("MantleMod: Pulling up...\r");
				*/
				m_mantlePhase = mantlingPulling;
				player->StartSound("snd_player_mantle_pull", SND_CHANNEL_VOICE, 0, false, NULL); // grayman #3010
				break;

			case mantlingPulling:
				/* we don't have a log system in our end
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("MantleMod: Shifting hand position...\r");
				*/
				m_mantlePhase = mantlingShiftHands;
				break;

			case mantlingShiftHands:
				/*in our end there isn't any log
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("MantleMod: Pushing self up...\r");
				*/
				m_mantlePhase = mantlingPushing;

				// Go into crouch
				current.movementFlags |= PMF_DUCKED;

				player->StartSound("snd_player_mantle_push", SND_CHANNEL_VOICE, 0, false, NULL); // grayman #3010
				break;

			case mantlingPushing:
				/* no log in our end
				DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("MantleMod: mantle completed\r");
				*/
				// check for clipping problems after mantling
				// will advance to notMantling when the player isn't clipping
				m_mantlePhase = fixTheClipping;

				// greebo: Reset the viewangle roll to 0 after mantling, sometimes this stays at 0.6 or something
				viewAngles.roll = 0;

				if (self != NULL)
				{
					//static_cast<idPlayer*>(self)->SetViewAngles(viewAngles);
					player->SetViewAngles(viewAngles);
				}

				break;

			default:
				m_mantlePhase = notMantling;
				break;
			}

			// Get time it takes to perform a mantling phase
			m_mantleTime = GetMantleTimeForPhase(m_mantlePhase);

			// Handle end of mantle
			if (m_mantlePhase == fixTheClipping)
			{
				// Handle end of mantle
				/* we haven't hidden the weapon since we don't have this immobilization system
				// Ishtvan 11/20/05 - Raise weapons after mantle is done
				static_cast<idPlayer*>(self)->SetImmobilization("MantleMove", 0);
				*/
				player->RaiseWeapon();
				player->SetInfluenceLevel( INFLUENCE_NONE );
			}
		}

		// Reduce mantle timer
		if (m_mantlePhase == fixTheClipping)
		{
			m_mantleTime = 0;
		}
		else
		{
			m_mantleTime -= framemSecLeft;
		}
	} // This code block is executed only if phase != notMantling && phase != fixClipping
}

/*
================
idPhysics_Player::CheckJumpHeldDown
================
*/
bool idPhysics_Player::CheckJumpHeldDown()
{
	return m_jumpHeldDownTime > pm_mantle_jump_hold_trigger.GetInteger();
}

/*
================
idPhysics_Player::MantleMove
================
*/
void idPhysics_Player::MantleMove()
{
	idVec3 newPosition = current.origin;
	idVec3 totalMove(0,0,0);
	float timeForMantlePhase = GetMantleTimeForPhase(m_mantlePhase);

	// Compute proportion into the current movement phase which we are
	float timeRatio = 0.0f;

	if (timeForMantlePhase != 0)
	{
		timeRatio = (timeForMantlePhase - m_mantleTime) /  timeForMantlePhase;
	}

	idPlayer* p_player = static_cast<idPlayer*>(self);

	// Branch based on phase
	if (m_mantlePhase == mantlingHanging)
	{
		// Starting at current position, hanging, rocking a bit.
		float rockDistance = 2.0f;

		newPosition = m_mantlePullStartPos;
		float timeRadians = idMath::PI * timeRatio;
		viewAngles.roll = idMath::Sin(timeRadians) * rockDistance;
		newPosition += (idMath::Sin(timeRadians) * rockDistance) * viewRight;

		if (self != NULL)
		{
			//static_cast<idPlayer*>(self)->SetViewAngles(viewAngles);
			p_player->SetViewAngles(viewAngles);
		}
	}
	else if (m_mantlePhase == mantlingPulling)
	{
		// Player pulls themself up to shoulder even with the surface
		totalMove = m_mantlePullEndPos - m_mantlePullStartPos;
		newPosition = m_mantlePullStartPos + (totalMove * idMath::Sin(timeRatio * (idMath::PI/2)) );
	}
	else if (m_mantlePhase == mantlingShiftHands)
	{
		// Rock back and forth a bit?
		float rockDistance = 1.0f;

		newPosition = m_mantlePullEndPos;
		float timeRadians = idMath::PI * timeRatio;
		newPosition += (idMath::Sin(timeRadians) * rockDistance) * viewRight;
		viewAngles.roll = idMath::Sin(timeRadians) * rockDistance;

		if (self != NULL)
		{
			//static_cast<idPlayer*>(self)->SetViewAngles(viewAngles);
			p_player->SetViewAngles(viewAngles);
		}
	}
	else if (m_mantlePhase == mantlingPushing)
	{
		// Rocking back and forth to get legs up over edge
		float rockDistance = 10.0f;

		// Player pushes themselves upward to get their legs onto the surface
		totalMove = m_mantlePushEndPos - m_mantlePullEndPos;
		newPosition = m_mantlePullEndPos + (totalMove * idMath::Sin(timeRatio * (idMath::PI/2)) );

		// We go into duck during this phase and stay there until end
		current.movementFlags |= PMF_DUCKED;

		float timeRadians = idMath::PI * timeRatio;
		newPosition += (idMath::Sin (timeRadians) * rockDistance) * viewRight;
		viewAngles.roll = idMath::Sin (timeRadians) * rockDistance;

		if (self != NULL)
		{
			//static_cast<idPlayer*>(self)->SetViewAngles(viewAngles);
			p_player->SetViewAngles(viewAngles);
		}
	}

	// If there is a mantled entity, positions are relative to it.
	// Transform position to be relative to world origin.
	// (For now, translation only, TODO: Add rotation)
	if (m_p_mantledEntity != NULL)
	{
		idPhysics* p_physics = m_p_mantledEntity->GetPhysics();
		if (p_physics != NULL)
		{
			// Ishtvan: Track rotation as well
			// newPosition += p_physics->GetOrigin();
			newPosition = p_physics->GetOrigin() + p_physics->GetAxis() * newPosition;
		}
	}

	SetOrigin(newPosition);
}

/*
================
idPhysics_Player::IsMantling
================
*/
bool idPhysics_Player::IsMantling() const
{
	return m_mantlePhase != notMantling && m_mantlePhase != fixTheClipping;
}

/*
================
idPhysics_Player::GetMantlePhase
================
*/
EMantlePhase idPhysics_Player::GetMantlePhase() const
{
	return m_mantlePhase;
}

/*
================
idPhysics_Player::CancelMantle
================
*/
void idPhysics_Player::CancelMantle()
{
	/* no logging feature on our end
	DM_LOG(LC_MOVEMENT, LT_DEBUG)LOGSTRING ("Mantle cancelled\r");
	*/
	idPlayer* player = static_cast<idPlayer*>(self);
	if ( player->GetInfluenceLevel() == INFLUENCE_LEVEL3 ) {
		player->SetInfluenceLevel( INFLUENCE_NONE );
	}
	static_cast<idPlayer*>(self)->RaiseWeapon();
	m_mantlePhase = notMantling;
	m_mantleTime = 0.0f;
}
