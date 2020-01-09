#include "Cosmetics.hpp"
#include "VapourSynth.h"
#include "VSHelper.h"

struct FilterData final {
	static constexpr auto filterName = "aaa";
	self(in, static_cast<const VSMap*>(nullptr));
	self(out, static_cast<VSMap*>(nullptr));
	self(api, static_cast<const VSAPI*>(nullptr));
	self(node, static_cast<VSNodeRef*>(nullptr));
	self(ref, static_cast<VSNodeRef*>(nullptr));
	self(vi, static_cast<const VSVideoInfo*>(nullptr));

	self(a, 0ll);
	self(s, 0ll);
	//self(s2, 0ll);
	self(h, 0.);
	self(h2, 0.);
	//self(sdev, 0.);


	self(process, std::array{ false,false,false });
	~FilterData() {
		if (node != nullptr)
			api->freeNode(node);
		if (ref != nullptr && ref != node)
			api->freeNode(ref);
	}

	auto CheckFormat() {
		auto errmsg = filterName + ": only single precision floating point clips with constant format and dimensions supported."s;
		if (vi->format == nullptr || vi->width == 0 || vi->height == 0 || vi->format->sampleType != stFloat || vi->format->bitsPerSample != 32) {
			api->setError(out, errmsg.data());
			return false;
		}
		return true;
	}
	auto CheckPlanes() {
		auto n = vi->format->numPlanes;
		auto m = std::max(api->propNumElements(in, "planes"), 0);
		auto errmsg1 = filterName + ": plane index out of range."s;
		auto errmsg2 = filterName + ": plane specified twice."s;
		for (auto& x : process)
			x = m == 0;
		for (auto i : Range{ m }) {
			auto o = api->propGetInt(in, "planes", i, nullptr);
			if (o < 0 || o >= n) {
				api->setError(out, errmsg1.data());
				return false;
			}
			if (process[o]) {
				api->setError(out, errmsg2.data());
				return false;
			}
			process[o] = true;
		}
		return true;
	}


	auto Initialize() {

		constexpr auto ScalingFactor = 79.636080791869483631941455867052;

		node = api->propGetNode(in, "clip", 0, nullptr);
		vi = api->getVideoInfo(node);
		auto err = 0;

		ref = api->propGetNode(in, "ref", 0, &err);
		if (err)
			ref = node;


		a = api->propGetInt(in, "a", 0, &err);
		if (err)
			a = 8;

		s = api->propGetInt(in, "s", 0, &err);
		if (err)
			s = 8;

		//s2 = api->propGetInt(in, "s2", 0, &err);
		//if (err)
			//s2 = s/2;

		h = api->propGetFloat(in, "h", 0, &err);
		if (err)
			h = 1.6;


		h2 = api->propGetFloat(in, "h2", 0, &err);
		if (err)
			h2 = h;

		h /= ScalingFactor;
		h2 /= ScalingFactor;

		//sdev = api->propGetFloat(in, "sdev", 0, &err);
		//if (err)
			//sdev = 1.;

		if (auto format_status = CheckFormat(); format_status == false)
			return false;
		if (auto plane_status = CheckPlanes(); plane_status == false)
			return false;
		return true;
	}
};

auto FilterInit = [](auto in, auto out, auto instanceData, auto node, auto core, auto vsapi) {
	auto d = reinterpret_cast<FilterData*>(*instanceData);
	vsapi->setVideoInfo(d->vi, 1, node);
};

auto FilterGetFrame = [](auto n, auto activationReason, auto instanceData, auto frameData, auto frameCtx, auto core, auto vsapi) {
	auto d = reinterpret_cast<const FilterData*>(*instanceData);
	auto nullframe = static_cast<const VSFrameRef*>(nullptr);
	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, d->node, frameCtx);
		if (d->ref != d->node)
			vsapi->requestFrameFilter(n, d->ref, frameCtx);
	}
	else if (activationReason == arAllFramesReady) {
		auto src = vsapi->getFrameFilter(n, d->node, frameCtx);
		auto ref = d->ref == d->node ? src : vsapi->getFrameFilter(n, d->ref, frameCtx);
		auto frames = std::array{
			d->process[0] ? nullframe : src,
			d->process[1] ? nullframe : src,
			d->process[2] ? nullframe : src
		};
		auto planes = std::array{ 0, 1, 2 };
		auto fmt = vsapi->getFrameFormat(src);
		auto dst = vsapi->newVideoFrame2(fmt, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), frames.data(), planes.data(), src, core);
		for (auto plane : Range{ fmt->numPlanes })
			if (d->process[plane]) {
				auto Square = [](auto x) { return x * x; };

				auto a = d->a;
				auto s = d->s;
				//auto s2 = d->s2;

				auto h = d->h;
				auto h2 = d->h2;

				//auto sdev = d->sdev;

				auto SearchSize = Square(2 * a + 1);
				auto PatchSize = Square(2 * s + 1);
				

				auto PatchMatrix = std::vector<std::vector<double>>{};
				auto PositionWeights = std::vector<double>{};
				auto PatchWeights = std::vector<double>{};
				auto CenterPatch = std::vector<double>{};
				//auto DistanceWeights = std::vector<double>{};

				PatchMatrix.resize(SearchSize);
				for (auto& x : PatchMatrix)
					x.resize(PatchSize);

				

				PositionWeights.resize(PatchSize);
				PatchWeights.resize(SearchSize);
				CenterPatch.resize(PatchSize);
				//DistanceWeights.resize(PatchSize);

				auto PlaneWidth = vsapi->getFrameWidth(src, plane);
				auto PlaneHeight = vsapi->getFrameHeight(src, plane);




				auto srcp = MakePlane<const float>(vsapi->getReadPtr(src, plane), PlaneWidth, PlaneHeight, Repeat);
				auto refp = MakePlane<const float>(vsapi->getReadPtr(ref, plane), PlaneWidth, PlaneHeight, Repeat);
				auto dstp = MakePlane<float>(vsapi->getWritePtr(dst, plane), PlaneWidth, PlaneHeight, Zero);


				auto MakePatchMatrix = [&](auto y, auto x) {
					auto MatrixCursor = PatchMatrix.begin();
					


					auto FlattenPatch = [&](auto FlattenedVector, auto &Canvas, auto y, auto x) {
						auto Cursor = FlattenedVector->begin();



						for (auto yCursor : Range{ y - s, y + s + 1 })
							for (auto xCursor : Range{ x - s, x + s + 1 }) {
								*Cursor = Canvas[yCursor][xCursor];
								++Cursor;
							}
					};


					FlattenPatch(&CenterPatch, srcp, y, x);

					for (auto yCursor : Range{ y - a, y + a + 1 })
						for (auto xCursor : Range{ x - a, x + a + 1 }) {
							FlattenPatch(MatrixCursor, refp, yCursor, xCursor);
							++MatrixCursor;
							
						}
				};

				/*
				auto CalculateDistanceWeights = [&]() {
					auto CalculateGaussianWeight = [&](auto y, auto x) {
						auto Variance = Square(sdev);
						auto SquaredDistance = Square(y - s) + Square(x - s);
						return std::exp(-SquaredDistance / (2. * Variance));
					};
					auto PatchView = MakePlane<double>(DistanceWeights.data(), 2 * s + 1, 2 * s + 1, Zero);
					for (auto y : Range{ 2 * s + 1 })
						for (auto x : Range{ 2 * s + 1 })
							PatchView[y][x] = CalculateGaussianWeight(y, x);
				};
				*/



				auto CalculatePatchWeights = [&]() {
					auto CalculatePatchSSE = [&](auto Patch) {
						auto SSE = 0.;
						for (auto x : Range{ PatchSize })
							SSE += Square(PatchMatrix[Patch][x] - PatchMatrix[(SearchSize - 1) / 2][x]);
						return SSE;
					};
					auto SSEToNormalizedWeights = [&]() {
						auto NormalizingConstant = 0.;
						for (auto& x : PatchWeights) {
							x = std::exp(-x / Square(h));
							NormalizingConstant += x;
						}
						for (auto& x : PatchWeights)
							x /= NormalizingConstant;
					};
					for (auto y : Range{ SearchSize })
						PatchWeights[y] = CalculatePatchSSE(y);
					SSEToNormalizedWeights();
				};




				auto CalculatePositionWeights = [&]() {
					auto CalculatePositionSSE = [&](auto Position) {
						auto SSE = 0.;
						for (auto y : Range{ SearchSize })
							SSE += PatchWeights[y] * Square(PatchMatrix[y][Position] - PatchMatrix[y][(PatchSize - 1) / 2]);
						return SSE;
					};
					auto SSEToWeights = [&]() {
						auto NormalizingConstant = 0.;
						for (auto& x : PositionWeights) {
							x = std::exp(-x / Square(h2));
							NormalizingConstant += x;
						}
						for (auto& x : PositionWeights)
							x /= NormalizingConstant;
					};
					for (auto x : Range{ PatchSize })
						PositionWeights[x] = CalculatePositionSSE(x);
					SSEToWeights();
				};







				auto Aggregate = [&]() {
					auto Result = 0.;
					for (auto x : Range{ PatchSize })
						Result += PositionWeights[x] * CenterPatch[x];
					return Result;
				};

				//CalculateDistanceWeights();
				for (auto y : Range{ PlaneHeight })
					for (auto x : Range{ PlaneWidth }) {
						MakePatchMatrix(y, x);
						CalculatePatchWeights();
						CalculatePositionWeights();
						dstp[y][x] = Aggregate();
					}



			}
			else
				continue;
		vsapi->freeFrame(src);
		return const_cast<decltype(nullframe)>(dst);
	}
	return nullframe;
};

auto FilterFree = [](auto instanceData, auto core, auto vsapi) {
	auto d = reinterpret_cast<FilterData*>(instanceData);
	delete d;
};

auto FilterCreate = [](auto in, auto out, auto userData, auto core, auto vsapi) {
	auto d = new FilterData{ in,out,vsapi };
	if (auto init_status = d->Initialize(); init_status == false) {
		delete d;
		return;
	}
	vsapi->createFilter(in, out, "Test", FilterInit, FilterGetFrame, FilterFree, fmParallel, 0, d, core);
};

VS_EXTERNAL_API(auto) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin* plugin) {
	configFunc("com.zonked.test", "test", "Test Filter", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("Test",
		"clip:clip;"
		"ref:clip:opt;"
		"a:int:opt;"
		"s:int:opt;"
		//"s2:int:opt;"
		"h:float:opt;"
		"h2:float:opt;"
		//"sdev:float:opt;"

		"planes:int[]:opt;"
		, FilterCreate, 0, plugin);
}
