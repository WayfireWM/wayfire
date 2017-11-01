#include "core.hpp"

namespace {
    int grabCount = 0;
}

Data::Data(){}
SubData::SubData() {}
SubData::~SubData() {}

void _Ownership::grab() {
    if(this->grabbed || !this->active)
        return;

    this->grabbed = true;
    grabCount++;

    if(grabCount == 1) {
        core->grab_pointer();
        core->grab_keyboard();
    }
}

void _Ownership::ungrab() {
    if(!grabbed || !active)
        return;

    grabbed = false;
    grabCount--;

    if(grabCount == 0) {
        core->ungrab_pointer();
        core->grab_keyboard();
    }

    if(grabCount < 0)
        grabCount = 0;
}

void Plugin::initOwnership() {
    owner->name = "Unknown";
    owner->compatAll = true;
}

void Plugin::updateConfiguration() {}
void Plugin::fini() {}

DataPair newIntOption(std::string name, int defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeInt;
    pair.second->def.ival = defaultVal;
    return pair;
}

DataPair newFloatOption(std::string name, float defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeFloat;
    pair.second->def.fval = defaultVal;
    return pair;
}

DataPair newBoolOption(std::string name, bool defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeBool;
    pair.second->def.bval = defaultVal;
    return pair;
}

DataPair newStringOption(std::string name, std::string defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeString;
    pair.second->def.sval = new std::string(defaultVal);
    return pair;
}

DataPair newColorOption(std::string name, Color defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeColor;
    pair.second->def.color = new Color(defaultVal);
    return pair;
}
DataPair newKeyOption(std::string name, Key defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeKey;
    pair.second->def.key = new Key(defaultVal);
    return pair;
}
DataPair newButtonOption(std::string name, Button defaultVal) {
    auto pair = std::make_pair(name, new Data());
    pair.second->type = DataTypeButton;
    pair.second->def.but = new Button(defaultVal);
    return pair;
}
