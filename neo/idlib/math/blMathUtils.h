/*
 * blMathUtils.h
 *
 *  Created on: 03/12/2016
 *      Author: Biel Bestué de Luna
 *      This file is licensed GPLv3
 */

#ifndef IDLIB_MATH_BLMATHUTILS_H_
#define IDLIB_MATH_BLMATHUTILS_H_


typedef struct ballistics_s {
	float				angle;		// angle in degrees in the range [-180, 180]
	float				time;		// time it takes before the projectile arrives
} ballistics_t;

class blMathUtils {
public:
	static float				MinNormalizeMax( float number, float max, float min );
	static int					ConeAlignment( idVec3 vec1, idVec3 vec2, float angle_threshold );
	int 						Ballistics( const idVec3 &start, const idVec3 &end, float speed, float gravity, ballistics_t bal[2] );
	static float 				HeightForTrajectory( const idVec3 &start, float zVel, float gravity );
	static idVec3				TimedBallistics( const float time, const idVec3 start, const idVec3 end, const idVec3 gravity_notNormalized );
	static idVec3				CappedAtBallistics( const float max_height, const idVec3 start, const idVec3 end, const idVec3 gravity_notNormalized );
};

#endif /* IDLIB_MATH_BLMATHUTILS_H_ */
