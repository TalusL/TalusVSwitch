
#ifndef ENTITYBASE_H
#define ENTITYBASE_H

#include <Util/mini.h>
#include <json/json.h>
#include <regex>

using namespace std;
using namespace toolkit;

/**
 * 解析json
 * @param jsonStr json字符串
 * @param value json对象
 * @return 解析是否成功
 */
inline bool parseJson(const string &jsonStr, Json::Value &value) {
    Json::Reader reader;
    return reader.parse(jsonStr, value);
}
/**
 * 从JSON中移除空节点
 * @param node
 */
inline void removeNullMember(Json::Value &node) {
    switch (node.type()) {
    case Json::ValueType::nullValue:
        return;
    case Json::ValueType::intValue:
        return;
    case Json::ValueType::uintValue:
        return;
    case Json::ValueType::realValue:
        return;
    case Json::ValueType::stringValue:
        return;
    case Json::ValueType::booleanValue:
        return;
    case Json::ValueType::arrayValue: {
        for (auto &child : node) {
            removeNullMember(child);
        }
        return;
    }
    case Json::ValueType::objectValue: {
        for (const auto &key : node.getMemberNames()) {
            auto &child = node[key];
            if (child.empty()) {
                node.removeMember(key);
            } else {
                removeNullMember(node[key]);
            }
        }
        return;
    }
    }
}
/**
 * Json转字符串
 * @param value json 对象
 * @return json 字符串
 */
inline string writeJson(const Json::Value &value, bool skipNull = false) {
    Json::FastWriter fastWriter;
    if (!skipNull) {
        return trim(fastWriter.write(value));
    }
    Json::Value out = value;
    removeNullMember(out);
    return trim(fastWriter.write(out));
}

/**
 * 实体基类
 */
class EntityBase : public Json::Value {
public:
    EntityBase() = default;
    EntityBase(const EntityBase &base)
        : Value(base) {
        fromJson(base);
    }
    /**
     * 从JSON中生成
     * @param value
     */
    virtual void fromJson(const Json::Value &value) {
        for (const auto &item : value.getMemberNames()) {
            (*this)[item] = value[item];
        }
    }
    /**
     * 从JSON字符中生成
     * @param str
     * @return
     */
    virtual bool fromJsonStr(const string &str) {
        Json::Value v;
        auto ret = parseJson(str, v);
        if (ret) {
            fromJson(v);
        }
        return ret;
    }
    /**
     * 转换为JSON字符串
     * @return
     */
    virtual string toJsonStr(bool skipNull = false) const { return writeJson(*this, skipNull); }


protected:
    /**
     * 通用初始化
     */
    class InitVar {
    public:
        template <typename T>
        InitVar(const string &name, vector<string> &members, Json::Value &v, const T &dv) {
            v = dv;
            members.push_back(name);
        }
        InitVar(const string &name, vector<string> &members) { members.push_back(name); }
    };
    // 成员名称
    vector<string> members;
};

// 转换宏
#define STR(x) #x
#define VAR(v)                                                                                                         \
public:                                                                                                                \
    Json::Value &v = (*this)[STR(v)];                                                                                  \
                                                                                                                       \
private:                                                                                                               \
    InitVar __init##v = InitVar(STR(v), members);                                                                      \
                                                                                                                       \
public:

#define VAR_DF(v, dv)                                                                                                  \
public:                                                                                                                \
    Json::Value &v = (*this)[STR(v)];                                                                                  \
                                                                                                                       \
private:                                                                                                               \
    InitVar __init##v = InitVar(STR(v), members, v, dv);                                                               \
                                                                                                                       \
public:

#define COPY_CONSTRUCTOR(T)                                                                                            \
public:                                                                                                                \
    T(const T &p)                                                                                                      \
        : EntityBase(p) {                                                                                              \
        fromJson(p);                                                                                                   \
    }

#define BASE_CONSTRUCTOR(T)                                                                                            \
public:                                                                                                                \
    T() = default;                                                                                                     \
    COPY_CONSTRUCTOR(T)

#endif
