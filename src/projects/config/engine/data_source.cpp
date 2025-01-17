//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "data_source.h"

#include <unistd.h>

#include <fstream>
#include <regex>

#include "config_error.h"
#include "item.h"

#define OV_LOG_TAG "Config.DataSource"

namespace cfg
{
	struct XmlWriter : pugi::xml_writer
	{
		ov::String result;

		void write(const void *data, size_t size) override
		{
			result.Append(static_cast<const char *>(data), size);
		}
	};

	DataSource::DataSource(const ov::String &current_path, const ov::String &file_name, const std::shared_ptr<pugi::xml_document> &document, const pugi::xml_node &node)
		: _type(DataType::Xml),
		  _document(document),
		  _node(node),
		  _current_path(current_path),
		  _file_name(file_name)
	{
		logtd("Trying to create a DataSource from XML value... [cwd: %s, file: %s]", current_path.CStr(), file_name.CStr());
	}

	DataSource::DataSource(const ov::String &current_path, const ov::String &file_name, const ov::String json_name, const Json::Value &json)
		: _type(DataType::Json),
		  _json_name(json_name),
		  _json(json),
		  _current_path(current_path),
		  _file_name(file_name)
	{
		logtd("Trying to create a DataSource from JSON value... [cwd: %s, file: %s]", current_path.CStr(), file_name.CStr());
	}

	DataSource::DataSource(DataType type, const ov::String &current_path, const ov::String &file_name, const ItemName &root_name)
		: _type(type),
		  _current_path(current_path)
	{
		ov::String file_name_to_load = file_name;

		if (ov::PathManager::IsAbsolute(file_name_to_load) == false)
		{
			if (_current_path.IsEmpty() == false)
			{
				file_name_to_load = ov::PathManager::Combine(ov::PathManager::ExtractPath(_current_path), file_name_to_load);
			}
		}

		_current_path = ov::PathManager::ExtractPath(file_name_to_load);

		logtd("Trying to create a DataSource for %s from %s file: %s [cwd: %s => %s, file: %s]",
			  root_name.ToString().CStr(),
			  (type == DataType::Xml) ? "XML" : "JSON",
			  file_name_to_load.CStr(),
			  current_path.CStr(), _current_path.CStr(),
			  file_name.CStr());

		LoadFromFile(file_name_to_load, root_name);
	}

	void DataSource::LoadFromFile(ov::String file_name, const ItemName &root_name)
	{
		_file_name = file_name;

		logtd("Trying to load data source from %s", file_name.CStr());

		switch (_type)
		{
			case DataType::Xml:
				LoadFromXmlFile(file_name, root_name.GetName(_type));
				return;

			case DataType::Json:
				LoadFromJson(file_name, root_name.GetName(_type));
				return;
		}

		throw CreateConfigError("Not implemented for type: %d (%s)", _type, file_name.CStr());
	}

	void DataSource::LoadFromXmlFile(const ov::String &file_name, const ov::String &root_name)
	{
		auto document = std::make_shared<pugi::xml_document>();

		pugi::xml_parse_result result = document->load_file(file_name);

		if (result == false)
		{
			throw CreateConfigError("Could not read the file: %s (reason: %s, offset: %td)",
									file_name.CStr(), result.description(), result.offset);
		}

		_document = document;
		_node = document->root().child(root_name);

		if (_node.empty())
		{
			throw CreateConfigError("Could not find the root element: %s in %s", root_name.CStr(), file_name.CStr());
		}
	}

	void DataSource::LoadFromJson(const ov::String &file_name, const ov::String &root_name)
	{
		std::ifstream json_file;
		json_file.open(file_name, std::ifstream::in | std::ifstream::binary);

		if (json_file.is_open())
		{
			json_file >> _json;
			json_file.close();
		}
		else
		{
			throw CreateConfigError("Could not read the file: %s", file_name.CStr());
		}
	}

	void DataSource::CheckUnknownItems(const ov::String &file_path, const ov::String &path, const std::map<ov::String, std::shared_ptr<Child>> &children_for_xml, const std::map<ov::String, std::shared_ptr<Child>> &children_for_json) const
	{
		switch (_type)
		{
			case DataType::Xml:
				for (auto &child_node : _node.children())
				{
					ov::String name(child_node.name());

					if (children_for_xml.find(name) == children_for_xml.end())
					{
						if (file_path.IsEmpty())
						{
							throw CreateConfigError("Unknown item found: %s.%s", path.CStr(), name.CStr());
						}
						else
						{
							throw CreateConfigError("Unknown item found: %s.%s in %s", path.CStr(), name.CStr(), file_path.CStr());
						}
					}
				}
				break;

			case DataType::Json: {
				if (_json.isObject())
				{
					auto members = _json.getMemberNames();

					for (auto &member : members)
					{
						ov::String name(member.c_str());

						if (name == "$")
						{
							// $ == attributes
							continue;
						}

						if (children_for_json.find(name) == children_for_json.end())
						{
							if (file_path.IsEmpty())
							{
								throw CreateConfigError("Unknown item found: %s.%s", path.CStr(), name.CStr());
							}
							else
							{
								throw CreateConfigError("Unknown item found: %s.%s in %s", path.CStr(), name.CStr(), file_path.CStr());
							}
						}
					}
				}

				break;
			}
		}
	}

	bool DataSource::IsArray(const ItemName &name) const
	{
		switch (_type)
		{
			case DataType::Xml:
				// if (_node)
				// {
				// 	auto iterator = _node.children(name.GetName(DataType::Json));
				// 	auto count = std::distance(iterator.begin(), iterator.end());
				// 	return count > 1;
				// }
				return true;

			case DataType::Json:
				return _json.isArray();
		}

		OV_ASSERT2(false);
		return false;
	}

	std::any DataSource::GetRootValue(ValueType value_type, bool resolve_path, OmitRule omit_rule, Json::Value *original_value) const
	{
		switch (_type)
		{
			case DataType::Xml:
				return GetValueFromXml(value_type, "", false, resolve_path, original_value);

			case DataType::Json:
				return GetValueFromJson(value_type, "", false, resolve_path, omit_rule, original_value);
		}

		OV_ASSERT2(false);
		return {};
	}

	std::any DataSource::GetValue(ValueType value_type, const ItemName &name, bool resolve_path, OmitRule omit_rule, Json::Value *original_value) const
	{
		switch (_type)
		{
			case DataType::Xml:
				return GetValueFromXml(value_type, name.GetName(_type), true, resolve_path, original_value);

			case DataType::Json:
				return GetValueFromJson(value_type, name.GetName(_type), true, resolve_path, omit_rule, original_value);
		}

		OV_ASSERT2(false);
		return {};
	}

	ov::String GetEnv(const char *key, const char *default_value, bool *is_default_value)
	{
		auto env = std::getenv(key);

		if (env != nullptr)
		{
			return env;
		}

		if (::strcmp(key, "HOSTNAME") == 0)
		{
			ov::String hostname;

			if (hostname.SetCapacity(HOST_NAME_MAX))
			{
				if (::gethostname(hostname.GetBuffer(), hostname.GetCapacity()) == 0)
				{
					return hostname;
				}
			}
		}

		if ((default_value != nullptr) && (default_value[0] != '\0'))
		{
			// Use the default value
			if (is_default_value != nullptr)
			{
				*is_default_value = true;
			}

			return default_value;
		}

		// There is no default value
		return "";
	}

	// Preprocess for ${env:XXX} macro
	ov::String PreprocessForEnv(const ov::String &value)
	{
		std::string str = value.CStr();
		std::string result;
		std::regex r(R"(\$\{env:([^}]*)\})");

		auto start = str.cbegin();

		std::sregex_iterator iterator = std::sregex_iterator(start, str.cend(), r);

		while (iterator != std::sregex_iterator())
		{
			std::smatch matches = *iterator;
			auto match = matches[0];
			auto token = matches[1];

			auto env_key = token.str();
			auto position = env_key.find_first_of(':');

			std::string default_value;
			bool is_default_value = false;

			if (position != std::string::npos)
			{
				default_value = env_key.substr(position + 1);
				env_key = env_key.substr(0, position);
			}

			auto key = env_key.c_str();
			ov::String value = GetEnv(key, default_value.c_str(), &is_default_value);

			result.append(start, match.first);
			result.append(value);

			start = match.second;

			++iterator;
		}

		result.append(start, str.cend());

		return result.c_str();
	}

	ov::String PreprocessForMacros(ov::String str)
	{
		return str
			.Replace("${ome.AppHome}", ov::PathManager::GetAppPath())
			.Replace("${ome.CurrentPath}", ov::PathManager::GetCurrentPath());
	}

	// Preprocess for ResolvePath annotation
	ov::String PreprocessForPath(const ov::String &current_path, ov::String str)
	{
		if (ov::PathManager::IsAbsolute(str) == false)
		{
			// relative path
			return ov::PathManager::Combine(current_path, str);
		}

		return str;
	}

	ov::String Preprocess(const ov::String &current_path, const ov::String &value, bool resolve_path)
	{
		ov::String result = PreprocessForEnv(value);

		result = PreprocessForMacros(result);

		if (resolve_path)
		{
			result = PreprocessForPath(current_path, result);
		}

		return result;
	}

	std::any DataSource::GetValueFromXml(ValueType value_type, const ov::String &name, bool is_child, bool resolve_path, Json::Value *original_value) const
	{
		switch (value_type)
		{
			case ValueType::Unknown:
				*original_value = Json::nullValue;
				return {};

			case ValueType::String: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = node.child_value();
				return node.empty() ? std::any() : Preprocess(_current_path, node.child_value(), resolve_path);
			}

			case ValueType::Integer: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = node.child_value();
				return node.empty() ? std::any() : ov::Converter::ToInt32(Preprocess(_current_path, node.child_value(), resolve_path));
			}

			case ValueType::Long: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = node.child_value();
				return node.empty() ? std::any() : ov::Converter::ToInt64(Preprocess(_current_path, node.child_value(), resolve_path));
			}

			case ValueType::Boolean: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = node.child_value();
				return node.empty() ? std::any() : ov::Converter::ToBool(Preprocess(_current_path, node.child_value(), resolve_path));
			}

			case ValueType::Double: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = node.child_value();
				return node.empty() ? std::any() : ov::Converter::ToDouble(Preprocess(_current_path, node.child_value(), resolve_path));
			}

			case ValueType::Attribute: {
				auto attribute = _node.attribute(name);
				*original_value = attribute.value();
				return attribute.empty() ? std::any() : Preprocess(_current_path, attribute.value(), resolve_path);
			}

			case ValueType::Text: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = node.child_value();
				return node.empty() ? std::any() : Preprocess(_current_path, node.child_value(), resolve_path);
			}

			case ValueType::Item: {
				auto &node = is_child ? _node.child(name) : _node;
				*original_value = Json::objectValue;
				return node.empty() ? std::any() : DataSource(_current_path, _file_name, _document, node);
			}

			case ValueType::List: {
				if (_node.empty() == false)
				{
					std::vector<DataSource> data_sources;

					if (original_value != nullptr)
					{
						*original_value = Json::arrayValue;
					}

					auto children = _node.children(name);

					for (auto &node_child : children)
					{
						data_sources.emplace_back(_current_path, _file_name, _document, node_child);

						if (original_value != nullptr)
						{
							original_value->append(node_child.child_value());
						}
					}

					if (data_sources.size() > 0)
					{
						return data_sources;
					}
				}

				return {};
			}
		}

		return {};
	}

	Json::Value GetJsonValue(const Json::Value &value, const ov::String &name)
	{
		if (value.isObject())
		{
			if (value.isMember(name))
			{
				return value[name];
			}
		}

		return Json::nullValue;
	}

	Json::Value GetJsonAttribute(const Json::Value &value, const ov::String &attribute_name)
	{
		if (value.isObject())
		{
			if (value.isMember("$"))
			{
				auto attributes = value["$"];

				return GetJsonValue(attributes, attribute_name);
			}
		}

		return Json::nullValue;
	}

	std::any GetJsonList(const ov::String &current_path, const ov::String &file_name, const Json::Value &json, const ov::String &name, OmitRule omit_rule, Json::Value *original_value)
	{
		if (json.isNull())
		{
			return {};
		}

		Json::Value child_value = Json::nullValue;

		if (json.isArray() == false)
		{
			if (omit_rule == OmitRule::Omit)
			{
				return GetJsonList(current_path, file_name, GetJsonValue(json, name), name, omit_rule, original_value);
			}
			else
			{
				child_value = GetJsonValue(json, name);

				if (child_value.isNull())
				{
					return {};
				}
			}
		}

		if (original_value != nullptr)
		{
			*original_value = Json::arrayValue;
		}

		std::vector<DataSource> data_sources;

		if (child_value.isNull())
		{
			for (auto &json_child : json)
			{
				data_sources.emplace_back(current_path, file_name, name, json_child);

				if (original_value != nullptr)
				{
					original_value->append(json_child);
				}
			}
		}
		else
		{
			data_sources.emplace_back(current_path, file_name, name, child_value);

			if (original_value != nullptr)
			{
				original_value->append(child_value);
			}
		}

		return data_sources;
	}

	std::any DataSource::GetValueFromJson(ValueType value_type, const ov::String &name, bool is_child, bool resolve_path, OmitRule omit_rule, Json::Value *original_value) const
	{
		switch (value_type)
		{
			case ValueType::Unknown:
				*original_value = Json::nullValue;
				return {};

			case ValueType::String: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : ov::Converter::ToString(Preprocess(_current_path, ov::Converter::ToString(json), resolve_path));
			}

			case ValueType::Integer: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : ov::Converter::ToInt32(json);
			}

			case ValueType::Long: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : ov::Converter::ToInt64(json);
			}

			case ValueType::Boolean: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : ov::Converter::ToBool(json);
			}

			case ValueType::Double: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : ov::Converter::ToDouble(json);
			}

			case ValueType::Attribute: {
				auto attribute = GetJsonAttribute(_json, name);
				*original_value = attribute;
				return attribute.empty() ? std::any() : Preprocess(_current_path, ov::Converter::ToString(attribute), resolve_path);
			}

			case ValueType::Text: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : Preprocess(_current_path, ov::Converter::ToString(json), resolve_path);
			}

			case ValueType::Item: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? std::any() : DataSource(_current_path, _file_name, name, json);
			}

			case ValueType::List: {
				if (_json.isNull() == false)
				{
					return GetJsonList(_current_path, _file_name, _json, name, omit_rule, original_value);
				}

				return {};
			}
		}

		return {};
	}

	ov::String DataSource::ToString() const
	{
		switch (_type)
		{
			case DataType::Xml: {
				XmlWriter writer;
				_node.print(writer);
				return writer.result;
			}

			case DataType::Json:
				return ov::Json::Stringify(_json, true);
		}

		return "";
	}
}  // namespace cfg
