/*
 * blMathUtils.cpp
 *
 *  Created on: 03/12/2016
 *      Author: Biel Bestué de Luna
 *      This file is licensed GPLv3
 */


#include "idlib/math/Math.h"
#include "idlib/math/Vector.h"
#include "idlib/math/blMathUtils.h"

/*
================
blMathUtils::MinNormalizeMax
================
*     //////////////////////////////////////////////////////////////////
*    //      return a normalized value between a min and a max       //
*   //////////////////////////////////////////////////////////////////
*/
float blMathUtils::MinNormalizeMax( float number, float max, float min ) {
    float interval, incorporated;

    interval = max - min;
    incorporated = number - min;

    if ( incorporated <= 0.0f ) {
    	return 0.0f;
    } else {
    	return ( ( 1 / interval ) * incorporated );
    }
}

/*
================
idMath::ConeAlignment

Checks alignments between vectors allowing for a "particular" miss-alignment with an angle threshold.
That miss-alignment can be massive :-)

if the vectors are aligned the dot product will be 1 if perpendicular will be 0 and opposed -1
if angle threshold is 0 cosine will be 1 if +/-180 it will be -1 and if +/-90 or 270 will be 0

returns -1 if wrong result ( in the case of one of the vectors being 0.0 in length )
================
*/
int blMathUtils::ConeAlignment( idVec3 vec1, idVec3 vec2, float angle_threshold ) {
	float dotProduct, cosine;

	//the dot product only represents the shadow over one vector of the other only if both vectors are normalized
	vec1.Normalize();
	vec2.Normalize();

	// the dot product is wrong if one of the vectors length is 0.0
	if ( (  vec1.Length() == 0.0f ) || ( vec2.Length() == 0.0f ) ) {
		return -1;
	}

	dotProduct = vec1 * vec2;
	cosine = idMath::Cos( DEG2RAD( idMath::Fabs( idMath::AngleNormalize180( angle_threshold ) ) ) );
	if ( dotProduct >= cosine ) {
		return 1;
	}
	return 0;
}


/*
=====================
Ballistics

  get the ideal aim pitch angle in order to hit the target
  also get the time it takes for the projectile to arrive at the target

  this is not mine, it's from ID's AI code
=====================
*/

int blMathUtils::Ballistics( const idVec3 &start, const idVec3 &end, float speed, float gravity, ballistics_t bal[2] ) {
	int n, i;
	float deltax, deltay, a, b, g_sqrt, d, sqrtd, inva, p[2];

	deltax = ( end.ToVec2() - start.ToVec2() ).Length();
	deltay = end[2] - start[2];

	a = 4.0f * deltay * deltay + 4.0f * deltax * deltax; // 4 * deltay² + 4 * deltax²
	b = -4.0f * speed * speed - 4.0f * deltay * gravity; // -4 * speed² -4 * deltay * g
	g_sqrt = gravity * gravity;

	d = b * b - 4.0f * a * g_sqrt;
	if ( d <= 0.0f || a == 0.0f ) {
		return 0;
	}
	sqrtd = idMath::Sqrt( d );
	inva = 0.5f / a;
	p[0] = ( - b + sqrtd ) * inva; 	// why this?
	p[1] = ( - b - sqrtd ) * inva;	// why this?
	n = 0;
	for ( i = 0; i < 2; i++ ) {
		if ( p[i] <= 0.0f ) { 		// why this?
			continue;
		}
		d = idMath::Sqrt( p[i] );
		bal[n].angle = atan2( 0.5f * ( 2.0f * deltay * p[i] - gravity ) / d, d * deltax );
		bal[n].time = deltax / ( cos( bal[n].angle ) * speed );
		bal[n].angle = idMath::AngleNormalize180( RAD2DEG( bal[n].angle ) );
		n++;
	}

	return n;
}

/*
=====================
HeightForTrajectory

Returns the maximum height of a given trajectory

this is not mine, it's from ID's AI code
=====================
*/
//#if 0
float blMathUtils::HeightForTrajectory( const idVec3 &start, float zVel, float gravity ) {
	float maxHeight, t;

	t = zVel / gravity;
	// maximum height of projectile
	maxHeight = start.z - 0.5f * gravity * ( t * t );

	return maxHeight;
}
//#endif

/*	ballistics: the 4 formulas:
 *
 * 		Y = 0.5 * a * t² + Voy * t
 * 		X =                Vox * t
 *
 * 		Vy = Voy + a * t
 * 		Vx = Vox
 *
 * 	Where a is the gravity which should be negative.
 */

idVec3 blMathUtils::TimedBallistics( const float time, const idVec3 start, const idVec3 end, const idVec3 gravity_notNormalized ) {
	idVec3 deltaVec, deltaNormal, g_normalized, x_component, result;
	float posX, posY, Vox, Voy, g, y_component;

	//first I'll prepare the

	deltaVec = end - start;
	deltaNormal = deltaVec;
	deltaNormal.Normalize();
	x_component = deltaVec;

	g = gravity_notNormalized.Length();
	g = -g;

	g_normalized = gravity_notNormalized;
	g_normalized.Normalize();

	x_component.ProjectOntoPlane( g_normalized, 1.001f );
	//now result is the projection of the vector between the two points on the gravity plane, so,
	//it's the projection of the X component of the motion

	//posX = Vox * time --> posX/time = Vox
	posX = x_component.Length();
	Vox = posX / time;
	x_component.Normalize();
	result = x_component * Vox;
	//now adding the Y component will be adding Voy to the [2] component of the result vector

	y_component = deltaNormal * -g_normalized;
	y_component *= deltaVec.Length();

	//delta_ycomponent is the projection of the delta to the inverted gravity vector (as gravity is down!)
	//posY = 0.5 * g * time * time + Voy * time ---> ( posY - ( 0.5 * g * time * time ) ) / time = Voy
	posY = y_component; // it could be negative and it would still be ok, as we can jump to a lower height too.
	Voy = ( posY - ( 0.5 * g * time * time ) ) / time;
	result[2] = Voy;

 	return result;
}
/*
=====================
blMathUtils::CappedAtBallistics

axiom: at max_height Vy is 0.
axiom: gravity is the same axis than the z axis.
=====================
*/
idVec3 blMathUtils::CappedAtBallistics( const float max_height, const idVec3 start, const idVec3 end, const idVec3 gravity_notNormalized ) {
	idVec3 result, g_normalized, deltaVec, deltaNormal, x_component;
	float	posX, posY, Vox, Voy, g, t_to_max, t_to_posY, y_component;

	// prepare the vectorial source data to setup a 2D problem with x and y components assuming the following
	// axiom: gravity is the same axis than the z axis.
	// this means x_compònent will remain a vector, and Y_component will be a float
	// the intention though is to get a vectorial result.

	deltaVec = end - start;
	deltaNormal = deltaVec;
	deltaNormal.Normalize();
	x_component = deltaVec;

	g = gravity_notNormalized.Length();
	g = -g;

	g_normalized = gravity_notNormalized;
	g_normalized.Normalize();

	y_component = deltaNormal * -g_normalized;
	y_component *= deltaVec.Length();
	posY = y_component;

	x_component.ProjectOntoPlane( g_normalized, 1.001f );
	posX = x_component.Length();

	//Vy = Voy + a * t
	//Y = 0.5 * a * t² + Voy * t
	//calulate how much it takes to reach max_height
	t_to_max = idMath::Sqrt( ( ( 2 * max_height ) / g ) );
	//use that time to get the Voy
	Voy = -g * t_to_max;
	//use that Voy to get a time to reach not max_height but posY
	//t = (Y / 0.5a ) -Voy
	t_to_posY = ( posY / ( 0.5 * g ) )-Voy;
	//use that time to get a Vox in the X component
	//X = Vox * t
	Vox = posX / t_to_posY; // because it takes the same time in both X and Y!

	//prepare the result
	x_component.Normalize();
	result = x_component * Vox;
	result[2] = Voy;

	return result;
}
