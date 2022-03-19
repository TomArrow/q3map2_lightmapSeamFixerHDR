

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include <string>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

#define AbsRatio(a,b) std::max((a)/(b),(b)/(a))
#define VectorCopy(a,b)			((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define DETECTIONRATIO 4
#define MINSTRIPLENGTH 4
#define Luminance(a) ((a)[0])*0.2126f+((a)[1])*0.7152f+((a)[2])*0.0722f
#define IsBlack(a) ((a)[0])==0.0f&&((a)[1])==0.0f&&((a)[2])==0.0f


void rotateImage90Degrees(float* data, int& w, int& h) {
	int pixelCount = w * h;
	float* tmp = new float[pixelCount * 3];
	for (int x = 0; x < w;x++) {
		for (int y = 0; y < h; y++) {
			VectorCopy(data+y * w * 3 + x * 3, tmp+x*h*3+y*3);
		}
	}
	memcpy(data, tmp, sizeof(float) * pixelCount*3);
	int tmpInt = w;
	w = h;
	h = tmpInt;
	delete[] tmp;
}

inline bool stripDetected(float*& data, int offset, int& w, int& h,int& detectionWidth) {
	int lineOffset = offset% w;
	bool leftIsNothing = offset > 0 ? data[offset * 3 - 3] == 0.0f : true;
	bool weAreAtCorner = (lineOffset == 0 || data[offset * 3] > data[offset * 3 - 3] * DETECTIONRATIO);
	if (weAreAtCorner && lineOffset < (w - detectionWidth - 1)) { // We are at corner and there's potentially enough room to actually detect a light strip.

		int detectedWidth = 1;
		while (AbsRatio(data[offset * 3], data[offset * 3 + 3]) < DETECTIONRATIO && detectedWidth < detectionWidth) {
			detectedWidth++;
			offset++;
		}
		if (detectedWidth < detectionWidth) {
			return false;
		}
		else {
			if (offset%w == w-1 || AbsRatio(data[offset * 3], data[offset * 3 + 3]) >= DETECTIONRATIO) {
				bool rightIsNothing = offset % w == w - 1 || data[offset * 3 + 3] == 0.0f;
				// Either this is a corner by detection or a corner since at border of image
				// Still return false if both left and right is nothing (nothing = either border of image or 0.0f). In that case it might be a legitimate strip of lightmap somewhere.
				return !(rightIsNothing && leftIsNothing);
			}
			else {
				return false;
			}
		}
	}
	else {
		return false;
	}
}


void process(std::string filename) {
	int overCorrect = 2;

	std::stringstream ss;
	ss << fs::path(filename).parent_path().string() << "\\fixed\\";
	std::string folder = ss.str();
	ss << fs::path(filename).filename().string();
	fs::create_directories(folder);
	std::cout << filename << " to " << ss.str()<< "\n";
	int w, h, c;
	float* data = stbi_loadf(filename.c_str(), &w, &h, &c, 3);

	int pixelCount = w * h;
	int stripsDetected = 0;

	for (int stripDetectionWidth = 1; stripDetectionWidth <= 5; stripDetectionWidth++) {
		for (int dir = 0; dir < 2; dir++) {
			if (dir == 1) {
				rotateImage90Degrees(data, w, h);
			}
			for (int i = 0; i < pixelCount; i++) {
				if (stripDetected(data, i, w, h, stripDetectionWidth)) {
					int detectedStripLength = 1;
					int newOffset = i + w;
					while (newOffset < pixelCount && stripDetected(data, newOffset, w, h, stripDetectionWidth)) {
						detectedStripLength++;
						newOffset += w;
					}
					if (detectedStripLength >= MINSTRIPLENGTH) {
						stripsDetected++;

						// Apply fix

						for (int l = -overCorrect; l < detectedStripLength+overCorrect; l++) { // Iterate through lines

							int firstStripPixelOffset = i + l * w;

							int colorSourceOffset = l < 0 ? 0-l:( l >= detectedStripLength ? detectedStripLength-l-1:0); // For overcorrecting lines, we take the color from one of the main detected lines

							int firstStripPixelOffsetForColor = i + (l + colorSourceOffset) * w;

							if (firstStripPixelOffset < 0 || firstStripPixelOffset > pixelCount) continue; // Since we are using overcorrecting, we might accidentally go outside the image. Account for that.

							float* pointerFirstStripPixel = data + firstStripPixelOffset * 3;
							float* pointerFirstStripPixelForColor = data + firstStripPixelOffsetForColor * 3;
							// Decide if we should take value from left or right
							// We simply take the brighter one or the one that isn't on another line
							float* dataPointerSrc;

							bool rightBorderReached = firstStripPixelOffset + stripDetectionWidth >= pixelCount;
							bool rightBorderReachedColor = firstStripPixelOffsetForColor + stripDetectionWidth >= pixelCount;

							float leftLuminance = (i%w) == 0 ? 0.0f : Luminance(pointerFirstStripPixelForColor -3);
							float rightLuminance = rightBorderReached ? 0.0f : Luminance(pointerFirstStripPixelForColor + stripDetectionWidth * 3 /*+ 3*/);

							if ((i%w == 0 || rightLuminance > leftLuminance) && !rightBorderReachedColor) {
								// Take from right
								dataPointerSrc = pointerFirstStripPixelForColor + stripDetectionWidth * 3 /*+ 3*/;
							}
							else {
								dataPointerSrc = pointerFirstStripPixelForColor -3;
							}

							for (int p = 0; p < stripDetectionWidth; p++) { // Iterate through strip pixels
								float* dataPointerDst = pointerFirstStripPixel + p * 3;
								//if (!colorSourceOffset || !IsBlack(dataPointerDst)) {
									// When we are overcorrecting, we don't want to overwrite black/blank space
									VectorCopy(dataPointerSrc, dataPointerDst);
								//}
							}
						}
					}


				}
			}
		}

	}
	// Rotate back to original orientation
	rotateImage90Degrees(data, w, h);
	rotateImage90Degrees(data, w, h);
	rotateImage90Degrees(data, w, h);

	std::cout << stripsDetected << " strips detected." << "\n";
	stbi_write_hdr(ss.str().c_str(), w, h, 3,data);
	stbi_image_free(data);
}


int main(int argc, char** argv) {

	for (int i = 1; i < argc; i++) {
		process(std::string(argv[i]));
	}

//#if DEBUG
	std::cin.get();
//#endif
}
