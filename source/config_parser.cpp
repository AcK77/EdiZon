#include "config_parser.hpp"
#include "title.hpp"

#include "widget_switch.hpp"
#include "widget_value.hpp"
#include "widget_list.hpp"
#include "widget_string.hpp"
#include "widget_comment.hpp"

#include <sstream>
#include <fstream>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <regex>
#include <iterator>
#include <locale>

#include "lua_interpreter.hpp"
#include "python_interpreter.hpp"

s8 ConfigParser::hasConfig(u64 titleId) {
    std::stringstream path;
    path << CONFIG_ROOT << std::setfill('0') << std::setw(sizeof(u64) * 2) << std::uppercase << std::hex << titleId << ".json";

    return ConfigParser::loadConfigFile(titleId, path.str(), nullptr);
}

s8 ConfigParser::loadConfigFile(u64 titleId, std::string filepath, Interpreter **interpreter) {
  std::ifstream file(filepath.c_str());
  if (file.fail())
    return 1;

  try {
    file >> ConfigParser::m_configFile;
  } catch (json::parse_error& e) {
		printf("Failed to parse JSON file.\n");
		return 2;
	}

  if (m_useInsteadTries++ > 5) {
    m_useInsteadTries = 0;
    return 4;
  }

  if (ConfigParser::m_configFile.find("useInstead") != ConfigParser::m_configFile.end()) {
    std::stringstream path;
    path << CONFIG_ROOT << ConfigParser::m_configFile["useInstead"].get<std::string>();
    return ConfigParser::loadConfigFile(titleId, path.str(), interpreter);
  }

  m_useInsteadTries = 0;

  if (ConfigParser::m_configFile.find("author") != ConfigParser::m_configFile.end())
    ConfigParser::g_currConfigAuthor = ConfigParser::m_configFile["author"];

  if (ConfigParser::m_configFile.find("beta") != ConfigParser::m_configFile.end())
    ConfigParser::g_betaTitles.insert({titleId, ConfigParser::m_configFile["beta"]});

  if (interpreter != nullptr) {
    if (ConfigParser::m_configFile.find("scriptLanguage") != ConfigParser::m_configFile.end()) {
      std::string language = ConfigParser::m_configFile["scriptLanguage"];

      std::transform(language.begin(), language.end(), language.begin(), ::tolower);

      if (language == "lua") *interpreter = new LuaInterpreter();
      else if (language == "py" || language == "python") *interpreter = new PythonInterpreter(); 
      else return 2;
    } else return 2;
  }

  if (ConfigParser::m_configFile.find("all") == ConfigParser::m_configFile.end()) {
    for (auto it : ConfigParser::m_configFile.items()) {
      if (it.key().find(Title::g_titles[titleId]->getTitleVersion()) != std::string::npos) {
        ConfigParser::m_configFile = ConfigParser::m_configFile[it.key()];

        if (ConfigParser::m_configFile == nullptr || !ConfigParser::m_configFile.is_array()) {
          ConfigParser::g_betaTitles.erase(titleId);
          return 2;
        }

        return 0;
      }
    }

    ConfigParser::g_betaTitles.erase(titleId);
    return 3;
  } else {
    ConfigParser::m_configFile = ConfigParser::m_configFile["all"];

    if (!ConfigParser::m_configFile.is_array()) {
      ConfigParser::g_betaTitles.erase(titleId);
      return 2;
    }

    return 0;
  }
}

void ConfigParser::createWidgets(WidgetItems &widgets, Interpreter &interpreter, u8 configIndex) {
  std::set<std::string> tempCategories;
  bool isDummy = false;

  if (ConfigParser::m_configFile == nullptr) return;

  if (!ConfigParser::m_configFile.is_array()) return;

  for (auto item : ConfigParser::m_configFile[configIndex]["items"]) {
    if (item["name"] == nullptr || item["category"] == nullptr || item["intArgs"] == nullptr || item["strArgs"] == nullptr) continue;

    auto itemWidget = item["widget"];
    if (itemWidget == nullptr) continue;
    if (itemWidget["type"] == nullptr) continue;

    if (item["dummy"] != nullptr)
      isDummy = item["dummy"].get<bool>();

    if (itemWidget["type"] == "int") {
      if (itemWidget["minValue"] == nullptr || itemWidget["maxValue"] == nullptr) continue;
      if (itemWidget["minValue"] >= itemWidget["maxValue"]) continue;
      widgets[item["category"]].push_back({ item["name"],
        new WidgetValue(&interpreter, isDummy,
          ConfigParser::getOptionalValue<std::string>(itemWidget, "readEquation", "value"),
          ConfigParser::getOptionalValue<std::string>(itemWidget, "writeEquation", "value"),
          itemWidget["minValue"], itemWidget["maxValue"],
          ConfigParser::getOptionalValue<u32>(itemWidget, "stepSize", 1)) });
    }
    else if (itemWidget["type"] == "bool") {
      if (itemWidget["onValue"] == nullptr || itemWidget["offValue"] == nullptr) continue;
      if (itemWidget["onValue"] == itemWidget["offValue"]) continue;
      if(itemWidget["onValue"].is_number() && itemWidget["offValue"].is_number()) {
        widgets[item["category"]].push_back({ item["name"],
          new WidgetSwitch(&interpreter, isDummy, itemWidget["onValue"].get<s32>(), itemWidget["offValue"].get<s32>()) });
      }
      else if(itemWidget["onValue"].is_string() && itemWidget["offValue"].is_string())
        widgets[item["category"]].push_back({ item["name"],
          new WidgetSwitch(&interpreter, isDummy, itemWidget["onValue"].get<std::string>(), itemWidget["offValue"].get<std::string>()) });
    }
    else if (itemWidget["type"] == "list") {
      if (itemWidget["listItemNames"] == nullptr || itemWidget["listItemValues"] == nullptr) continue;

      if (itemWidget["listItemValues"][0].is_number()) {
        widgets[item["category"]].push_back({ item["name"],
          new WidgetList(&interpreter, isDummy, itemWidget["listItemNames"], itemWidget["listItemValues"].get<std::vector<s32>>()) });
      }
      else if (itemWidget["listItemValues"][0].is_string())
        widgets[item["category"]].push_back({ item["name"], 
          new WidgetList(&interpreter, isDummy, itemWidget["listItemNames"], itemWidget["listItemValues"].get<std::vector<std::string>>()) });
    } 
    else if (itemWidget["type"] == "string") {
      if (itemWidget["minLength"] == nullptr || itemWidget["maxLength"] == nullptr) continue;

      if (itemWidget["minLength"].is_number() && itemWidget["maxLength"].is_number()) {
        widgets[item["category"]].push_back({ item["name"], 
          new WidgetString(&interpreter, isDummy, itemWidget["minLength"].get<u8>(), itemWidget["maxLength"].get<u8>()) });
      }
    }
    else if (itemWidget["type"] == "comment") {
      if (itemWidget["comment"] == nullptr) continue;

      if (itemWidget["comment"].is_string()) {
        widgets[item["category"]].push_back({ item["name"], 
          new WidgetComment(&interpreter, itemWidget["comment"].get<std::string>()) });
      }
    }

    widgets[item["category"]].back().widget->setLuaArgs(item["intArgs"], item["strArgs"]);

    tempCategories.insert(item["category"].get<std::string>());;
    Widget::g_selectedRow = CATEGORIES;
  }

  Widget::g_categories.clear();
  std::copy(tempCategories.begin(), tempCategories.end(), std::back_inserter(Widget::g_categories));
  Widget::g_selectedCategory = Widget::g_categories[0];

  for (auto category : tempCategories)
    Widget::g_widgetPageCnt[category] = ceil(widgets[category].size() / WIDGETS_PER_PAGE);
}
