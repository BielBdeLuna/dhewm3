/*
 * blMathUtils.h
 *
 *  Created on: 03/12/2016
 *      Author: Biel Bestué de Luna
 *      This file is licensed GPLv3
 */

#ifndef IDLIB_MATH_BLMATHUTILS_H_
#define IDLIB_MATH_BLMATHUTILS_H_

class blMathUtils {
public:
	static float				MinNormalizeMax( float number, float max, float min );
	static int					ConeAlignment( idVec3 vec1, idVec3 vec2, float angle_threshold);

};


#endif /* IDLIB_MATH_BLMATHUTILS_H_ */
