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
