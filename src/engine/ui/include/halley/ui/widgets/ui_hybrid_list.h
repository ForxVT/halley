#pragma once
#include "ui_list.h"

namespace Halley {
    class UIHybridList : public UIWidget {
    public:
		explicit UIHybridList(const String& id, UIStyle style, UISizerType orientation = UISizerType::Vertical, int nColumns = 1);

		void addTextItem(const String& id, const LocalisedString& label);
	    void addDivider();

		void setInputButtons(const UIInputButtons& button);
		void setItemEnabled(const String& id, bool enabled);

    private:
		UIStyle style;
		std::shared_ptr<UIList> list;
		std::shared_ptr<UIWidget> buttons;
		std::shared_ptr<UIWidget> cancelButton;
		int nColumns;
		UIInputButtons uiInputButtons;
    };
}
