#include <random>
#include <string>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <assert.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

size_t IntPow(size_t base, size_t exp)
{
	size_t ret = 1;
	while (exp--)
	{
		ret *= base;
	}
	return ret;
}

size_t ComputeElementCount(size_t dimCount, const size_t sizePerDim[])
{
	size_t elemCount = 1;
	for (size_t currDim = 0; currDim < dimCount; ++currDim)
	{
		elemCount *= sizePerDim[currDim];
	}
	return elemCount;
}

enum EMethod
{
	Method_SolidAngle,
	Method_HighPass,
};

const bool   useIncrementalUpdate = true;
const size_t N_dimensions = 2;
const size_t dimensionSize[N_dimensions] = { 128, 128 };
const size_t N_valuesPerItem = 1;
const size_t totalElements = ComputeElementCount(N_dimensions, dimensionSize);

const EMethod chosenMethod = Method_SolidAngle;

// Solid Angle method parameters
const size_t distanceToCheck = 3; // original paper mentioned looking at whole array; however this is N^2 and super expensive, while exp(-4*4/2.2) ~= 0.000694216
const size_t distanceToCheckBoth = distanceToCheck * 2 + 1; // in both directions

const size_t elementsToCheck = IntPow(distanceToCheckBoth, N_dimensions);

const size_t numIterationsToFindDistribution = 256 * 1024;


// Highpass filter parameters
const size_t convSize				 = 3;
const size_t convSizeTotal			 = IntPow(convSize, N_dimensions);

const float convWeights1D[3]		 = { -1, 2, -1 };

const float convWeights2D[3*3]		 = { -1, -2, -1, 
									 	 -2, 12, -2, 
									 	 -1, -2, -1 };

const float convWeights3D[3 * 3 * 3] = { -1, -2, -1, 
										 -2, -4, -2, 
									     -1, -2, -1, 

										 -2, -4, -2,
										 -4, 56, -4,
										 -2, -4, -2,

										 -1, -2, -1,
										 -2, -4, -2,
										 -1, -2, -1,
};

void PrintCodeOutput(const std::string& fileName, const std::vector<float>& arr, const std::string& arrName, bool mathematica, const size_t dimensionSize[N_dimensions], size_t N_dimensions, size_t N_valuesPerItem)
{
	std::ofstream outFile;
	outFile.open(fileName, std::ios::out | std::ios::trunc);

	const size_t arrSize = arr.size();

	if (!mathematica)
	{
		outFile << "static const float " << arrName;
		for (size_t d = 0; d < N_dimensions; ++d)
		{
			outFile << "[" << dimensionSize[d] << "]";
		}
		if (N_valuesPerItem > 1)
		{
			outFile << "[" << N_valuesPerItem << "]";
		}
		outFile << " = " << std::endl;
	}


	for (size_t i = 0; i < arrSize / N_valuesPerItem; ++i)
	{
		for (size_t d = 0; d < N_dimensions; ++d)
		{
			size_t dim = (i / ComputeElementCount(d, dimensionSize)) % dimensionSize[d];

			if (dim == 0)
			{
				outFile << "{";
			}
			else
			{
				break;
			}
		}

		if (N_valuesPerItem == 1)
		{
			outFile << std::setprecision(8) << std::fixed << arr[i];
		}
		else
		{
			outFile << "{";
			for (size_t v = 0; v < N_valuesPerItem; ++v)
			{
				outFile << std::setprecision(8) << std::fixed << arr[i * N_valuesPerItem + v];
				if (v < N_valuesPerItem - 1)
				{
					outFile << ", ";
				}
			}
			outFile << "}";
		}

		for (size_t d = 0; d < N_dimensions; ++d)
		{
			size_t dim = (i / ComputeElementCount(d, dimensionSize)) % dimensionSize[d];

			if (dim == dimensionSize[d] - 1)
			{
				outFile << "}";
			}
			else
			{
				outFile << ",";
				if (d > 0)
				{
					outFile << std::endl;
				}
				break;
			}
		}
	}
	if (!mathematica)
	{
		outFile << ";";
	}

	outFile << std::endl << std::endl;
}

std::string dimNames[4] = { "x", "y", "z", "w" };

void PrintWebGLOutputRecursive(std::ofstream& outFile, const std::vector<float>& arr, const std::string& arrName, size_t N_dimensions, size_t N_valuesPerItem, size_t lo, size_t high)
{
	if (high - lo == 1)
	{
		outFile << "return ";

		if (N_valuesPerItem > 1)
		{
			outFile << "vec" << N_valuesPerItem << "(";
			for (size_t v = 0; v < N_valuesPerItem; ++v)
			{
				outFile << arr[lo * N_valuesPerItem + v];
				if (v < N_valuesPerItem - 1)
				{
					outFile << ", ";
				}
			}

			outFile << ");";
		}
		else
		{
			outFile << arr[lo] << ";";
		}
	}
	else
	{
		size_t mid = (lo + high) / 2;

		outFile << "if(" << arrName << " < " << mid << ") " << std::endl << "{" << std::endl;
		PrintWebGLOutputRecursive(outFile, arr, arrName, N_dimensions, N_valuesPerItem, lo, mid);
		outFile << "} else {" << std::endl;
		PrintWebGLOutputRecursive(outFile, arr, arrName, N_dimensions, N_valuesPerItem, mid, high);
		outFile << std::endl << "}";
	}
};

void PrintWebGLOutput(const std::string& fileName, const std::vector<float>& arr, const std::string& arrName, size_t N_dimensions, size_t N_valuesPerItem, size_t lo, size_t high)
{
	std::ofstream outFile;
	outFile.open(fileName, std::ios::out | std::ios::trunc);

	PrintWebGLOutputRecursive(outFile, arr, arrName, N_dimensions, N_valuesPerItem, lo, high);
}

inline uint32_t FloatAsByteUNorm(float value)
{
	return uint32_t(255.f * value);
}

void SaveAsPPM(const std::vector<float>& arr, const std::string& fileName, const size_t dimensionSize[N_dimensions], size_t N_dimensions, size_t N_valuesPerItem)
{
	std::ofstream outfile(fileName);
	assert(N_dimensions == 2);
	outfile << "P3" << std::endl << dimensionSize[0] << " " << dimensionSize[1] << std::endl << 255 << std::endl;
	const uint32_t pixCount = arr.size() / N_valuesPerItem;
	for (size_t i = 0; i < pixCount; ++i)
	{
		switch (N_valuesPerItem)
		{
		case 1: // monichromatic values
			outfile << FloatAsByteUNorm(arr[i]) << " "
				<< FloatAsByteUNorm(arr[i]) << " "
				<< FloatAsByteUNorm(arr[i]) << " ";
			break;
		case 2:
			outfile << FloatAsByteUNorm(arr[i * 2]) << " "
				<< FloatAsByteUNorm(arr[i * 2 + 1]) << " "
				<< 0 << " ";
			break;
		case 3:
			outfile << FloatAsByteUNorm(arr[i * 3]) << " "
				<< FloatAsByteUNorm(arr[i * 3 + 1]) << " "
				<< FloatAsByteUNorm(arr[i * 3 + 2]) << " ";
			break;
		default:
			assert(0);
		}
	}
	outfile.close();
}

inline float ComputeFinalScore(const std::vector<float>& arr, float distanceScore, size_t N_valuesPerItem, size_t ind1, size_t ind2)
{
	float valueSpaceScore = 0;
	for (size_t i = 0; i < N_valuesPerItem; ++i)
	{
		float val = (arr[ind1 * N_valuesPerItem + i] - arr[ind2 * N_valuesPerItem + i]);
		valueSpaceScore += val * val;
	}

	valueSpaceScore = powf(valueSpaceScore, (float)N_valuesPerItem / 2.0f);
	const float oneOverDistanceVarianceSq = 1.0f / (2.1f * 2.1f);

	return expf(-valueSpaceScore - distanceScore * oneOverDistanceVarianceSq);
}

inline float ComputeDistanceScore(const int arr[], size_t Ndimensions)
{
	float distanceSq = 0;
	for (size_t i = 0; i < Ndimensions; ++i)
	{
		distanceSq += arr[i] * arr[i];
	}
	return distanceSq;
}

double sign(double v) { return (v >= 0.0) ? 1.0 : -1.0; }
double remap_tri( const double v )
{
	double r2 = 0.5 * v;
	double f1 = sqrt( r2 );
	double f2 = 1.0 - sqrt( r2 - 0.25 );
	return (v < 0.5) ? f1 : f2;
}

//note: splats to 24b
uint8_t* FloatDataToBytes(const std::vector<float>& arr, size_t dimensionSize, size_t N_valuesPerItem, bool do_remap_tri)
{
	uint8_t* bytes = new uint8_t[3 * arr.size() / N_valuesPerItem];

	for (size_t i = 0, n = arr.size() / N_valuesPerItem; i<n; ++i)
	{
		uint8_t lastChar = 0;
		for (size_t j = 0; j < N_valuesPerItem; j++)
		{
			double v_f = arr[i * N_valuesPerItem + j];

			if (do_remap_tri)
				v_f = remap_tri(v_f);

			int v_i = static_cast<int>(v_f * 256.0);
			v_i = (v_i < 0) ? 0 : v_i;
			v_i = (v_i > 255) ? 255 : v_i;

			lastChar = static_cast<char>(v_i);

			bytes[3 * i + j] = lastChar;
		}

		for (size_t j = N_valuesPerItem; j < 3; j++)
		{
			// splat
			if (N_valuesPerItem == 1)
			{
				bytes[3 * i + j] = lastChar;
			}
			else
			{
				bytes[3 * i + j] = 0;
			}
		}
	}

	return bytes;
}

//note: see http://gpuopen.com/vdr-follow-up-fine-art-of-film-grain/
void UnifyHistogram(std::vector<float>& arr, size_t N_valuesPerItem)
{
	for (size_t dim = 0; dim < N_valuesPerItem; dim++)
	{
		std::vector<std::pair<float, size_t> > entries(arr.size() / N_valuesPerItem);

		for (size_t i = 0, n = arr.size() / N_valuesPerItem; i < n; ++i)
		{
			entries[i] = std::make_pair(arr[i * N_valuesPerItem + dim], i);
		}

		std::sort(entries.begin(), entries.end(), [](const std::pair<float, size_t>& a, const std::pair<float, size_t>& b) -> bool
		{
			return a.first < b.first;
		});

		for (size_t i = 0, n = entries.size(); i<n; ++i)
		{
			float t = static_cast<float>(i) / static_cast<float>(n-1);
			size_t idx = entries[i].second;
			arr[idx * N_valuesPerItem + dim] = t;
		}
	}
}

inline size_t WrapDimension(size_t baseIndex, int offset, size_t dimSize)
{
	int posWrapped = (int)baseIndex + offset;
	if (posWrapped < 0)
	{
		posWrapped += dimSize;
	}
	if (posWrapped > (int)(dimSize - 1))
	{
		posWrapped -= dimSize;
	}

	return posWrapped;
}

int main(int argc, char** argv)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<> dist(0, 1);
	// Note: we try to swap between 1 and 3 elements to try to jump over local minima
	const int maxSwapedElemCount = 3;
	std::uniform_int_distribution<> distInt(1, maxSwapedElemCount);
	std::uniform_int_distribution<> distSwap(0, (int)(totalElements - 1));

	std::vector<float> pattern[2] = { std::vector<float>(totalElements * N_valuesPerItem), std::vector<float>(totalElements * N_valuesPerItem) };
	uint32_t currentArray = 0;

	for (size_t i = 0; i < totalElements * N_valuesPerItem; ++i)
	{
		pattern[currentArray][i] = static_cast<float>(dist(gen));
	}

	UnifyHistogram(pattern[currentArray], N_valuesPerItem);

	SaveAsPPM(pattern[0], "D:\\white_noise.ppm", dimensionSize, N_dimensions, N_valuesPerItem);

	//PrintCodeOutput("initialDist.txt", pattern[currentArray], "initialDist", true, dimensionSize, N_dimensions, N_valuesPerItem);

	if (chosenMethod == Method_SolidAngle)
	{
		std::vector<float> distanceWeights(elementsToCheck);

		for (size_t i = 0; i < elementsToCheck; ++i)
		{
			int distances[N_dimensions];
			for (size_t d = 0; d < N_dimensions; ++d)
			{
				size_t dim = (i / IntPow(distanceToCheckBoth, d)) % distanceToCheckBoth;
				distances[d] = (int)dim - distanceToCheck;
			}

			distanceWeights[i] = ComputeDistanceScore(distances, N_dimensions);
		}

		std::chrono::milliseconds time_start_ms = std::chrono::duration_cast<std::chrono::milliseconds >(
			std::chrono::system_clock::now().time_since_epoch());

		float bestScore = std::numeric_limits<float>::max();

		std::vector<bool>   touchedElemBits(totalElements, false);
		std::vector<size_t> touchedElemIndex;

		auto ComputeScore = [&](size_t srcElem, uint32_t currArray) -> float
		{
			float score = 0.f;
			for (size_t elem = 0; elem < elementsToCheck; ++elem)
			{
				size_t j = 0;

				for (size_t d = 0; d < N_dimensions; ++d)
				{
					size_t sourceDim = (srcElem / ComputeElementCount(d, dimensionSize)) % dimensionSize[d];
					size_t offsetDim = (elem / IntPow(distanceToCheckBoth, d)) % distanceToCheckBoth;

					int offset = (int)offsetDim - distanceToCheck;

					j += WrapDimension(sourceDim, offset, dimensionSize[d]) * ComputeElementCount(d, dimensionSize);
				}
				if (srcElem == j)
					continue;
				score += ComputeFinalScore(pattern[currArray], distanceWeights[elem], N_valuesPerItem, srcElem, j);
			}
			assert(score >= 0.f);
			return score;
		};

		auto MarkModifiedElems = [&](size_t srcElem) -> void
		{
			float score = 0.f;
			for (size_t elem = 0; elem < elementsToCheck; ++elem)
			{
				size_t j = 0;

				for (size_t d = 0; d < N_dimensions; ++d)
				{
					size_t sourceDim = (srcElem / ComputeElementCount(d, dimensionSize)) % dimensionSize[d];
					size_t offsetDim = (elem / IntPow(distanceToCheckBoth, d)) % distanceToCheckBoth;

					int offset = (int)offsetDim - distanceToCheck;

					j += WrapDimension(sourceDim, offset, dimensionSize[d]) * ComputeElementCount(d, dimensionSize);
				}
				if (touchedElemBits[j] == false)
				{
					touchedElemIndex.push_back(j);
					touchedElemBits[j] = true;
				}
			}
		};

		if (useIncrementalUpdate)
		{
			pattern[currentArray ^ 1] = pattern[currentArray]; // both array start equal
			bestScore = 0.f;
			for (size_t i = 0; i < totalElements; ++i)
			{
				bestScore += ComputeScore(i, 0);
			}
		}

		for (size_t iter = 0; iter < numIterationsToFindDistribution; ++iter)
		{
			if (useIncrementalUpdate && totalElements >= (18 * 18)) // incremental version becomes interesting when there are more than 18 x 18 elements to handle
			{
				uint32_t num_swaps = distInt(gen);
				size_t swapedElemIndex[maxSwapedElemCount * 2];
				for (size_t i = 0; i < num_swaps; ++i)
				{
					size_t from = distSwap(gen);
					size_t to = distSwap(gen);
					while (from == to)
						to = distSwap(gen);

					swapedElemIndex[2 * i] = from;
					swapedElemIndex[2 * i + 1] = to;

					// mark region where score must be recomputed
					MarkModifiedElems(to);
					MarkModifiedElems(from);

					for (size_t vecDim = 0; vecDim < N_valuesPerItem; ++vecDim)
					{
						std::swap(pattern[0][from * N_valuesPerItem + vecDim], pattern[0][to * N_valuesPerItem + vecDim]);
					}
				}

				float scoreToRemove = 0.f;
				for (size_t elemIndex : touchedElemIndex)
				{
					scoreToRemove += ComputeScore(elemIndex, 1); // remove score from previous distribution
				}

				float scoreToAdd = 0.f;
				for (size_t elemIndex : touchedElemIndex)
				{
					scoreToAdd += ComputeScore(elemIndex, 0); // add score from current distribution
					touchedElemBits[elemIndex] = false;
				}

				float deltaScore = scoreToAdd - scoreToRemove;
				touchedElemIndex.clear();

				if (deltaScore < 0.f)
				{
					bestScore += deltaScore;
					// commit changes to other array
					for (uint32_t i = 0; i < num_swaps * 2; ++i)
					{
						const int modifiedIndex = swapedElemIndex[i];
						for (size_t vecDim = 0; vecDim < N_valuesPerItem; ++vecDim)
						{
							pattern[1][modifiedIndex * N_valuesPerItem + vecDim] = pattern[0][modifiedIndex * N_valuesPerItem + vecDim];
						}
					}
				}
				else
				{
					// rollback changes from other array
					for (uint32_t i = 0; i < num_swaps * 2; ++i)
					{
						const int modifiedIndex = swapedElemIndex[i];
						for (size_t vecDim = 0; vecDim < N_valuesPerItem; ++vecDim)
						{
							pattern[0][modifiedIndex * N_valuesPerItem + vecDim] = pattern[1][modifiedIndex * N_valuesPerItem + vecDim];
						}
					}
				}
			}
			else
			{
				// version with global score update
				// copy
				pattern[currentArray ^ 1] = pattern[currentArray];

				uint32_t num_swaps = distInt(gen);
				for (size_t i = 0; i < num_swaps; ++i)
				{
					size_t from = distSwap(gen);
					size_t to = distSwap(gen);
					while (from == to)
						to = distSwap(gen);

					for (size_t vecDim = 0; vecDim < N_valuesPerItem; ++vecDim)
					{
						std::swap(pattern[currentArray][from * N_valuesPerItem + vecDim], pattern[currentArray][to * N_valuesPerItem + vecDim]);
					}
				}
				float score = 0.0f;

				for (size_t i = 0; i < totalElements; ++i)
				{
					score += ComputeScore(i, currentArray);
				}

				if (score < bestScore)
				{
					bestScore = score;
				}
				else
				{
					// swap back
					currentArray ^= 1;
				}
			}

			if (iter>0 && (iter % (numIterationsToFindDistribution / 100) == 0))
			{
				std::chrono::milliseconds time_ms = std::chrono::duration_cast<std::chrono::milliseconds >(
					std::chrono::system_clock::now().time_since_epoch());

				std::chrono::milliseconds elapsed = time_ms - time_start_ms;

				float pct = static_cast<float>(iter) / static_cast<float>(numIterationsToFindDistribution);
				float est_remain = static_cast<float>(elapsed.count()) / pct * (1 - pct);
				est_remain /= 1000.0f;

				std::cout << iter << "/" << numIterationsToFindDistribution << " best score: " << bestScore << " eta: " << static_cast<int>(est_remain) << "s" << std::endl;
			}
		}
	}
	else if (chosenMethod == Method_HighPass)
	{
		const float* convArr = nullptr;

		if (N_dimensions == 1)
			convArr = convWeights1D;
		else if (N_dimensions == 2)
			convArr = convWeights2D;
		else
			convArr = convWeights3D;

		for (size_t iter = 0; iter < 4; iter++)
		{
			// copy
			pattern[currentArray ^ 1] = pattern[currentArray];

			for (size_t i = 0; i < totalElements; ++i)
			{
				for (size_t vectorItem = 0; vectorItem < N_valuesPerItem; ++vectorItem)
				{
					float convSum = 0.0f;
					for (size_t elem = 0; elem < convSizeTotal; ++elem)
					{
						size_t j = 0;

						for (size_t d = 0; d < N_dimensions; ++d)
						{
							size_t sourceDim = (i / ComputeElementCount(d, dimensionSize)) % dimensionSize[d];
							size_t offsetDim = (elem / IntPow(convSize, d)) % convSize;

							int offset = (int)offsetDim - convSize / 2;

							j += WrapDimension(sourceDim, offset, dimensionSize[d]) * ComputeElementCount(d, dimensionSize);
						}

						convSum += pattern[currentArray ^ 1][j * N_valuesPerItem + vectorItem] * convArr[elem];
					}

					pattern[currentArray][i * N_valuesPerItem + vectorItem] = convSum;
				}
			}

			UnifyHistogram(pattern[currentArray], N_valuesPerItem);
		}
	}
	else
	{
		std::abort();
	}

	//PrintCodeOutput("finalDist.txt", pattern[currentArray], "finalDist", true, dimensionSize, N_dimensions, N_valuesPerItem);
	//PrintWebGLOutput("webgl.txt", pattern[currentArray], "finalDist", dimensionSize, N_dimensions, N_valuesPerItem, 0, totalElements);

	/*if (N_dimensions == 2)
	{
		char filename[512];
		memset(filename, 0, 512);
		sprintf(filename, "output_%dx%d_uni.bmp", (int)dimensionSize, (int)dimensionSize);
		uint8_t* bytedata = FloatDataToBytes(pattern[currentArray], dimensionSize, N_valuesPerItem, false);
		stbi_write_bmp(filename, dimensionSize, dimensionSize, 3, bytedata);
		std::cout << "wrote " << filename << std::endl;
		delete[] bytedata;

		memset(filename, 0, 512);
		sprintf(filename, "output_%dx%d_tri.bmp", (int)dimensionSize, (int)dimensionSize);
		bytedata = FloatDataToBytes(pattern[currentArray], dimensionSize, N_valuesPerItem, true);
		stbi_write_bmp(filename, dimensionSize, dimensionSize, 3, bytedata);
		std::cout << "wrote " << filename << std::endl;
		delete[] bytedata;
	}*/

	SaveAsPPM(pattern[0], "D:\\blue_noise.ppm", dimensionSize, N_dimensions, N_valuesPerItem);

	return 0;
}
