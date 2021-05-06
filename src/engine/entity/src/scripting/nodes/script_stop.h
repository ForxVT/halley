#pragma once
#include "scripting/script_environment.h"

namespace Halley {	
	class ScriptStop final : public ScriptNodeTypeBase<void> {
	public:
		String getId() const override { return "stop"; }
		String getName() const override { return "Stop"; }
		String getIconName(const ScriptGraphNode& node) const override { return "script_icons/stop.png"; }
		gsl::span<const PinType> getPinConfiguration() const override;
		std::pair<String, std::vector<ColourOverride>> getNodeDescription(const ScriptGraphNode& node, const World& world) const override;
		ScriptNodeClassification getClassification() const override { return ScriptNodeClassification::Terminator; }
		Result doUpdate(ScriptEnvironment& environment, Time time, const ScriptGraphNode& node) const override;
	};
}