#pragma once
#include "Score.h"
#include "Math.h"
#include "EffectView.h"

namespace MikuMikuWorld
{
	struct Score;
	struct Note;
	struct ScoreContext;
}

namespace MikuMikuWorld::Engine
{
	struct DrawingNote
	{
		id_t refID;
		int layer;
		Range visualTime;
	};

	struct DrawingLine
	{
		Range xPos;
		Range visualTime;
	};

	struct DrawingHoldTick
	{
		id_t refID;
		int layer;
		float center;
		Range visualTime;
	};

	struct DrawingHoldSegment
	{
		id_t endID;
		id_t holdStartID;
		EaseType ease;
		bool isGuide;
		GuideColor guideColor;
		ptrdiff_t tailStepIndex;
		double headTime, tailTime;
		float headLeft, headRight;
		float tailLeft, tailRight;
		float startTime, endTime;
		double activeTime;
		int startLayer, endLayer;
	};

	struct DrawData
	{
		float noteSpeed;
		int maxTicks;
		std::vector<DrawingNote> drawingNotes;
		std::vector<DrawingLine> drawingLines;
		std::vector<DrawingHoldTick> drawingHoldTicks;
		std::vector<DrawingHoldSegment> drawingHoldSegments;
		Effect::EffectView effectView;

		void clear();
		void calculateDrawData(Score const& score);
	};
}
