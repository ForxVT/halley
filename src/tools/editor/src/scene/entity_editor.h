#pragma once
#include "halley/ui/ui_widget.h"

namespace Halley {
	class UIFactory;

	class EntityEditor final : public UIWidget {
	public:
		EntityEditor(String id, UIFactory& factory);

	private:
		UIFactory& factory;

		void makeUI();
	};
}