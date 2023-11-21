#include <stack>
#include <fstream>
#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <unordered_map>
#include <phpcpp.h>

//https://textual.textualize.io/widgets/static/
//https://www.php-cpp.com/documentation/calling-functions-and-methods

#define VERBOSE 1
#define DEBUG 1

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#if VERBOSE
#define vprintf(...) fprintf(stderr, __VA_ARGS__)
#else
#define vprintf(...)
#endif

#if DEBUG
#define dprintf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dprintf(...)
#endif

bool g_sandbox = false;


using namespace std;
//using json = nlohmann::json;

namespace NIMS {

enum ScopeType {
    EXEC, // [f foo]
    ASSIGN, // {foo: bar}
    STR, // foo
    SYM,
    RAW
};

typedef Php::Object context_t;
using Array = Php::Array;
using Value = Php::Value;
using Object = Php::Object;
//using json = Php::Value;

string seed;

string hash(string v) {
    return Php::eval("return hash('md5','" + v + "');");
}

string json_to_string(Value j) {
    return Php::call("json_encode", j);
}

Value json_parse(string s) {
    return Php::call("json_decode", s);
}

string stringify(Value j) {
    if (j.isString()) {
        return j;
    }
    return json_to_string(j);
}

Value hash_value(Value v) {
    if (v.isObject() || v.isArray()) {
        for (auto& it : v) {
            Value vv = it.second;
            v[it.first] = hash_value(vv);
        }
        return v;
    }
    string s = stringify(v);
    return hash(s);
}

void push_back(Array& arr, Value val) {
    arr[arr.size()] = val;
}

string file_get_contents(string path) {
    return Php::call("file_get_contents", path);
}
string str_trim(string path) {
    return Php::call("trim", path);
}

string server_read_file(string path) {
    return Php::call("__read_file", path);
}

Array a_slice_i(Array& arr, int start) {
    dprintf("Going into a_slice_i\n");
    Value res = Php::call("array_slice", arr, start);
    dprintf("Got out of a_slice_i: %s\n", stringify(res).c_str());
    Array a = res;
    dprintf("After in func cast\n");
    return a;
}

void send_message(string& s) {
    write(1, s.c_str(), s.size());
    write(1, "\n", 1);
}

bool is_numeric(string s) {
    for (auto it = s.begin(); it != s.end(); it++) {
        if (!isdigit(*it) && *it != '.' && *it != 'e' && *it != 'E' && *it != '+' && *it != '-' && *it != 'x' && *it != 'X') {
            return false;
        }
    }
    return true;
}

bool is_alphanumeric(string s) {
    for (auto it = s.begin(); it != s.end(); it++) {
        if (!isalnum(*it)) {
            return false;
        }
    }
    return true;
}

bool is_identifier_char(char c) {
    if (!isalnum(c) && c != '_') {
        return false;
    }
    return true;
}

bool is_identifier(string s) {
    if (s.size() == 0) {
        return false;
    }
    for (auto it = s.begin(); it != s.end(); it++) {
        if (!is_identifier_char(*it)) {
            return false;
        }
    }
    return true;
}


string Parse(string data) {
    stack<pair<ScopeType, size_t>> scope;
    scope.push({ScopeType::EXEC, 0});
    bool did_comma = true;

    for(auto it = data.begin(); ; it++) {
        size_t ind = it - data.begin();

        // Current scope
        ScopeType cs = scope.top().first;
        if (it != data.end()) {
            vprintf("%u: Checking char `%c`\n", ind, *it); 

            // Skip whitespace in non-plaintext scopes
            if (cs != ScopeType::RAW && cs != ScopeType::STR && cs != ScopeType::SYM) {
                // skip spaces
                if (*it != '\n' && isspace(*it)) {
                    continue;
                }
            }

            // Check for ends of scopes
            if (cs == ScopeType::EXEC) {
                if (*it == ']') {
                    scope.pop();
                    vprintf("Popped EXEC scope\n");
                    did_comma = false;
                    continue;
                }
            }
            if (cs == ScopeType::ASSIGN) {
                if (*it == '}') {
                    scope.pop();
                    vprintf("Popped ASSIGN scope\n");
                    did_comma = false;
                    continue;
                } else if (*it == ':') {
                    did_comma = true;
                    // skip
                    continue;
                }
            }

            if (cs == ScopeType::EXEC || cs == ScopeType::ASSIGN) {
                if (*it == ',') {
                    did_comma = true;
                    continue;
                }
                else if (*it == '[') {
                    scope.push({ScopeType::EXEC, ind});
                    vprintf("Pushed EXEC scope\n");
                } else if (*it == '{') {
                    scope.push({ScopeType::ASSIGN, ind});
                    vprintf("Pushed ASSIGN scope\n");
                } else if (*it == '"') {
                    scope.push({ScopeType::STR, ind});
                    vprintf("Pushed STR scope\n");

                } else if (cs == ScopeType::EXEC && *it != '\n' && (it == data.begin() || *(it - 1) == '\n') ) {
                    scope.push({ScopeType::RAW, ind});
                    vprintf("Pushed RAW scope\n");
                } else if (is_identifier_char(*it)) {
                    scope.push({ScopeType::SYM, ind});
                    vprintf("Pushed SYM scope\n");
                } else {
                    continue;
                }
                if (!did_comma) {
                    data.insert(it, ',');
                    vprintf("Inserted `,` at %u\n", ind);
                    it = data.begin() + ind + 1;
                    scope.top().second++;
                    if (scope.top().first != ScopeType::EXEC && scope.top().first != ScopeType::ASSIGN) {
                        did_comma = false;
                    } else {
                        did_comma = true;
                    }
                }
                continue;
            }
            if (cs == ScopeType::STR) {
                if (*it == '\\') {
                    // parse escape
                    it++;
                } else if (*it == '"') {
                    scope.pop();
                    vprintf("Popped STR scope\n");
                }
            }
        }
        bool end_sym = false;
        if (cs == ScopeType::SYM) {
            if (it == data.end() || !is_identifier_char(*it)) {
                end_sym = true;
            }
        }
        if (cs == ScopeType::RAW) {
            if (it == data.end() || *it == '\n') {
                end_sym = true;
            }
        }

        if (end_sym) {
            data.insert(it, '"');
            vprintf("Inserted `\"` at %u\n", ind);
            it = data.begin() + ind + 1;
            size_t old_ind = scope.top().second;

            // insert " at ind pos
            data.insert(old_ind, "\"");
            vprintf("Inserted `\"` at %u\n", old_ind);
            it = data.begin() + ind + 1;
            scope.pop();
            vprintf("Popped SYM/RAW scope\n");
            //cout << data << endl;

            did_comma = false;
        }
        if (it >= data.end()) {
            break;
        }
    }
    return data;
}

string readFile(const string& fileName) {
    ifstream file(fileName);
    if (!file.is_open()) {
        cerr << "Error opening file: " << fileName << endl;
        return "";
    }
    string content((istreambuf_iterator<char>(file)), (istreambuf_iterator<char>()));
    file.close();
    return content;
}



Array collect_args(context_t& context, Array& args);
Value function_call(context_t& context, Array& func, Array& args, bool collected = false);
Value process_entry(context_t& context, Value& entry, bool do_assign = false);
Value process_function(context_t& context, Array& tree, Array* document);

std::map<string, Value> g_context;
std::map<string, void*> g_functions;


Value f_print(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    for (int i = 0; i < args.size(); i++) {
        if (i != 0) {
            printf(" ");
        }
        printf("%s", stringify(args[i]).c_str());
    }
    printf("\n");
    return nullptr;
}

Value f_concat(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string s = "";
    for (int i = 0; i < args.size(); i++) {
        s += stringify(args[i]); 
    }
    return s;
}

Value f_add(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    if (args.size() == 0) {
        return nullptr;
    }
    Value first = args[0];
    if (first.isString()) {
        return f_concat(ctx, args, true);
    }
    double v = 0;
    for (int i = 0; i < args.size(); i++) {
        double d = args[i];
        v += d;
    }
    return v;
}

Value f_array_for_each(context_t& ctx, Array& itr, Array& func) {
    for (int i = 0; i < itr.size(); i++) {
        Array args = {itr[i]};

        function_call(ctx, func, args, true);
    }
    return nullptr;
}

Value make_function(Array args, Array body) {
    Array func;
    push_back(func, args);
    for (int i = 0; i < body.size(); i++) {
        push_back(func, body[i]);
    }
    return std::move(func);
}

Value f_get_property(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }
    Value o = args[0];
    Value k = args[1];
    return o[k];
}


Value f_for(context_t& ctx, Array& args, bool _) {
    if (args.size() < 2) {
        eprintf("Error: [for { v : itr } ...] expected 0\n");
        return nullptr;
    }
    Value as = args[0];
    if (!as.isObject()) {
        eprintf("Error: [for { v : itr } ...] expected 1\n");
        return nullptr;
    }
    if (as.size() != 1) {
        eprintf("Error: [for { v : itr } ...] expected 2\n");
        return nullptr;
    }
    Object a = as;
    string v_name = a.begin()->first;
    Value _itr = a.begin()->second;

    Value itr = process_entry(ctx, _itr, false);


    Array _body = a_slice_i(args, 1);

    if (itr.isArray()) {
        Array _itr = itr;
        Array f = make_function({ v_name }, _body);
        return f_array_for_each(ctx, _itr, f);
    }
    eprintf("Error: iterator must be an array\n");
    return nullptr;
}

Value f_return(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    if (args.size() == 0) {
        return nullptr;
    }
    Array r;
    push_back(r, "__INTERNAL_RETURN");
    push_back(r, args[0]);
    return std::move(r);
}

bool g_allow_render = true;
bool g_allow_message = true;
Array g_current_document;
context_t* g_root_ctx = nullptr;

std::pair<size_t, Value> find_id(string id) {
    for(size_t i=0; i<g_current_document.size(); i++) {
        Value v = g_current_document[i];
        if (!v.isObject()) {
            continue;
        }
        Object o = v;

        if (!o.contains("id")) {
            continue;
        }

        Value _o_id = o["id"];
        string o_id = _o_id;

        if (o_id != id) {
            continue;
        }

        return {i, v};
    }
    return {0, nullptr};
}


Value f_find_id(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string id = args[0];
    return find_id(id).second;
}

Value f_replace_id(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string id = args[0];
    auto el = find_id(id);
    if (!el.second.isObject()) {
        return nullptr;
    }

    Value vv = args[1];
    g_current_document[el.first] = vv;
    return nullptr;
}

bool is_user(string target) {
    if (!Php::GLOBALS["_SESSION"].contains("user")) {
        return false;
    }
    string v = Php::GLOBALS["_SESSION"]["user"];
    if (v != target) {
        return false;
    }
    return true;
}

Value f_message_admin(context_t& ctx, Array& args, bool collected) {
    if (!g_allow_message) {
        return "Failed to message admin, already in admin message...";
    }
    string uri = args[0];
    //*
    string safe = Php::call("escapeshellarg", uri);

    string cmd = "php /src/server/server.php " + safe;
    dprintf("Calling command: `%s`\n", cmd.c_str());

    Value v = Php::call("popen", cmd, "r");
    Value r = Php::call("fread", v, 4096);
    string rr = stringify(r);

    Value resp = json_parse(rr);
    if (!resp.isObject()) {
        return rr;
    }
    if (!resp.contains("body")) {
        return rr;
    }
    Value body = resp["body"];

    if (!is_user("admin")) {
        return hash_value(body);
    }

    return body;
}


Value f_replace_document(context_t& ctx, Array& args, bool collected) {
    Value _arg = args[0];
    Array arg = _arg;

    g_current_document = arg;
    return std::move(arg);
}

Value f_get_document(context_t& ctx, Array& args, bool _) {
    return g_current_document;
}

Value f_render(context_t& ctx, Array& args, bool collected) {
    if (!g_allow_render) {
        eprintf("Failed to call eval, already evaling");
        return nullptr;
    }
    g_allow_render = false;

    if (!collected) { args = collect_args(ctx, args); }

    Object render_context = context_t(ctx);

    Value _tree = args[0];
    Array tree = _tree;

    Value res = process_function(render_context, tree, nullptr);
    g_allow_render = true;
    return res;
}

Object create_element(string type, Value body, Value opts) {
    Object o;
    if (opts.isObject()) {
        o = opts;
    }
    o["element"] = type;
    o["body"] = body;
    return o;
}

std::map<string, std::pair<Object, Array>> g_internal_actions;


// BUG eval injection
// BUG resets seed to "" for crafted input
string generate_id(string name) {
    // Generate random id
    // sha
    string v = hash(seed);
    //string v = Php::eval("return 'md5';");
    seed = v;
    return name + "_" + v;
}

Value get_or_null(Array& o, size_t index) {
    if (o.size() > index) {
        return o[index];
    }
    return nullptr;
}
Value get_or_null(Object& o, string key) {
    if (o.contains(key)) {
        return o[key];
    }
    return nullptr;
}
Value get_or_null(Value& o, string key) {
    if (o.contains(key)) {
        return o[key];
    }
    return nullptr;
}
Value get_or_null(Array& o, string key) {
    if (o.contains(key)) {
        return o[key];
    }
    return nullptr;
}

Value f_get_args(context_t& ctx, Array& args, bool _) {
    return Php::GLOBALS["_ARGS"];
}
Value f_get_session(context_t& ctx, Array& args, bool _) {
    return Php::GLOBALS["_SESSION"];
}

Value f_include(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string path = args[0];
    string content = file_get_contents(path);

    string res = "[\n" + Parse(content) + "\n]";

    dprintf("Formated result: %s\n", res.c_str());
    Value j = json_parse(res);

    Array nargs = {j};
    Value rres = f_render(ctx, nargs, true);
    if (rres.isArray() && rres.size() == 1) {
        return rres[0];
    }
    return rres;
}

Value f_element_button(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }
    dprintf("after collect args\n");

    Value arg = args[0];
    string action = generate_id("action");

    dprintf("Adding action %s\n", action.c_str());
    Value _action = args[1];
    Array action_arr = _action;
    std::pair<Object, Array> p(ctx, action_arr);
    g_internal_actions[action] = p;

    dprintf("Creating element\n");
    Object o = create_element("button", arg, get_or_null(args,2));
    o["action"] = action;

    return std::move(o);
}
Value f_element_tag(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string tag_name = args[0];

    Array rest = a_slice_i(args, 1);

    Object o = create_element("tag", rest, nullptr);
    o["id"] = tag_name;
    return std::move(o);
}
Value f_element_row(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string tag_name = args[0];

    Array rest = a_slice_i(args, 1);

    Object o = create_element("row", rest, nullptr);
    o["id"] = tag_name;
    return std::move(o);
}

Value f_element_head(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    Object o = create_element("head", args[0], get_or_null(args, 1));
    return std::move(o);
}
Value f_element_input(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string name = args[0];
    Object o = create_element("input", nullptr, get_or_null(args, 1));
    o["name"] = name;


    return std::move(o);
}
Value f_style(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }

    string path = args[0];

    string new_css = server_read_file(path);

    string css = (*g_root_ctx)["__css"];
    css += new_css;
    (*g_root_ctx)["__css"] = css;

    return nullptr;
}
Value f_navigate(context_t& ctx, Array& args, bool collected) {
    if (!collected) { args = collect_args(ctx, args); }
    string arg = args[0];

    Object o;
    o["uri"] = arg;
    o["args"] = get_or_null(args, 1);
    (*g_root_ctx)["__navigate_target"] = o;
    return nullptr;
}

Value f_dict(context_t& ctx, Array& args, bool _) {
    Value v = args[0];
    if (v.isObject()) {
        return v;
    }
    Object o;
    return std::move(o);
}

void Init() {




}

Array make_internal_id(string& key) {
    return {key};
}

void init_runtime(context_t& ctx, Value document, bool restricted) {
    string key = generate_id("__INTERNAL_PRINT");
    g_functions[key] = (void*)f_print;
    ctx["print"] = Array{key};

    key = generate_id("__INTERNAL_ADD");
    g_functions[key] = (void*)f_add;
    ctx["add"] = Array{key};

    key = generate_id("__INTERNAL_FOR");
    g_functions[key] = (void*)f_for;
    ctx["for"] = Array{key};

    key = generate_id("__INTERNAL_FOR_EACH");
    g_functions[key] = (void*)f_array_for_each;
    ctx["foreach"] = Array{key};

    key = generate_id("__INTERNAL_DICT");
    g_functions[key] = (void*)f_dict;
    ctx["dict"] = Array{key};

    key = generate_id("__INTERNAL_GET_PROPERTY");
    g_functions[key] = (void*)f_get_property;
    ctx["get"] = Array{key};

    key = generate_id("__INTERNAL_RETURN");
    g_functions[key] = (void*)f_return;
    ctx["return"] = Array{key};

    key = generate_id("__INTERNAL_EVAL");
    g_functions[key] = (void*)f_render;
    ctx["eval"] = Array{key};

    key = generate_id("__INTERNAL_GET_DOCUMENT");
    g_functions[key] = (void*)f_get_document;
    ctx["document"] = Array{key};

    key = generate_id("__INTERNAL_GET_ARGS");
    g_functions[key] = (void*)f_get_args;
    ctx["args"] = Array{key};

    key = generate_id("__INTERNAL_REPLACE_DOCUMENT");
    g_functions[key] = (void*)f_replace_document;
    ctx["set_document"] = Array{key};

    key = generate_id("__INTERNAL_ELEMENT_BUTTON");
    g_functions[key] = (void*)f_element_button;
    ctx["button"] = Array{key};

    key = generate_id("__INTERNAL_ELEMENT_INPUT");
    g_functions[key] = (void*)f_element_input;
    ctx["input"] = Array{key};

    key = generate_id("__INTERNAL_ELEMENT_TAG");
    g_functions[key] = (void*)f_element_tag;
    ctx["tag"] = Array{key};

    key = generate_id("__INTERNAL_ELEMENT_ROW");
    g_functions[key] = (void*)f_element_row;
    ctx["row"] = Array{key};

    key = generate_id("__INTERNAL_ELEMENT_HEAD");
    g_functions[key] = (void*)f_element_head;
    ctx["head"] = Array{key};

    key = generate_id("__INTERNAL_STYLE");
    g_functions[key] = (void*)f_style;
    ctx["style"] = Array{key};

    key = generate_id("__INTERNAL_NAVIGATE");
    g_functions[key] = (void*)f_navigate;
    ctx["navigate"] = Array{key};

    key = generate_id("__INTERNAL_REPLACE_ID");
    g_functions[key] = (void*)f_replace_id;
    ctx["replace"] = Array{key};

    key = generate_id("__INTERNAL_FIND_ID");
    g_functions[key] = (void*)f_find_id;
    ctx["find"] = Array{key};

    key = generate_id("__INTERNAL_MESSAGE_ADMIN");
    g_functions[key] = (void*)f_message_admin;
    ctx["message_admin"] = Array{key};

    ctx["__document"] = document;
    ctx["__INTERNAL_CALLBACKS"] = Object();
    ctx["__css"] = "";
    ctx["__navigate_target"] = nullptr;

    g_current_document = Array();

    if (!restricted) {
        key = generate_id("__INTERNAL_INCLUDE");
        g_functions[key] = (void*)f_include;
        ctx["include"] = Array{key};
    }
}


Value process_string(context_t& context, string s) {
    if (is_identifier(s)) {
        // symbol, do lookup
        if (!context.contains(s)) {
            if (is_numeric(s)) {
                // Bare numbers
                return stod(s);
            }
            // Bare words
            return s;
        }
        Value v = context[s];
        dprintf("## Loaded key `%s` from context -> `%s`\n", s.c_str(), stringify(v).c_str());
        return v;
    }
    // Raw text
    return s;
}

Value process_assign(context_t& context, Object& map) {
    for (auto it = map.begin(); it != map.end(); it++) {
        string key = it->first;
        if (!is_identifier(key)) {
            eprintf("Invalid identifier `%s`\n", key.c_str());
            continue;
        }
        Value val = it->second;
        Value res = process_entry(context, val, false);
        context[key] = res;
        dprintf("V_%s := `%s`\n", key.c_str(), stringify(res).c_str());
    }
    return nullptr;
}

Value process_object(context_t& context, Object& map) {
    for (auto it = map.begin(); it != map.end(); it++) {
        string key = it->first;
        Value val = it->second;
        Value res = process_entry(context, val, false);
        map[it->first] = val;
    }
    return map;
}

Value last_result(Array* args) {
    if (args->size() == 0) {
        return nullptr;
    }
    return (*args)[args->size() - 1];
}

Value get_return(Value v) {
    if (!v.isArray()) {
        return nullptr;
    }
    Array a = v;
    if (a.size() < 2) {
        return nullptr;
    }
    Value b = a[0];
    if (!b.isString()) {
        return nullptr;
    }
    string c = b;
    if (c != "__INTERNAL_RETURN") {
        return nullptr;
    }
    return a[1];
}

Value process_function(context_t& context, Array& tree, Array* document) {
    //bool has_str_output = false;
    Array a;
    Array* output = document;
    if (output == nullptr) {
        output = &a;
    }

    for (auto& it : tree) {
        Value v = it.second;
        Value res = process_entry(context, v, true);
        if (res.isNull()) {
            continue;
        }

        dprintf("checking if return....\n");
        Value ret = get_return(res);
        dprintf("past get_return\n");
        if (!ret.isNull())  {
            return ret;
        }

        push_back(*output, res);
    }
    dprintf("LEAVING FUNCTION\n");
    Value j = *output;
    dprintf("~~~ Returning `%s`\n", stringify(j).c_str());
    if (output->size() == 0) {
        return nullptr;
    }
    if (output->size() == 1) {
        return output[0];
    }
    return *output;
}

Value call_native(context_t& context, string func_name, Array args, bool collected=true) {
    auto it = g_functions.find(func_name);
    if (it == g_functions.end()) {
        eprintf("Unknown function `%s`\n", func_name.c_str());
        return nullptr;
    }
    Value(*fp)(context_t&, Array&, bool) = (Value(*)(context_t&, Array&, bool))it->second;
    dprintf(",--- Calling native function `%s` at %p\n", func_name.c_str(), fp);

    Value res = fp(context, args, collected);
    dprintf("'--- Native function `%s` returned `%s`\n", func_name.c_str(), stringify(res).c_str());
    return res;
}


Array collect_args(context_t& context, Array& args) {
    Array res;
    for (int i = 0; i < args.size(); i++) {
        Value arg = args[i];
        Value res_arg = process_entry(context, arg, false);
        push_back(res, res_arg);
    }
    return res;
}

Value call_vm(context_t& context, Array& func, Array& args) {
    if (func.size() < 2) {
        return nullptr;
    }
    Value first = func[0];
    Array arg_names = first;

    for (int i = 0; i < arg_names.size(); i++) {
        string arg_name = arg_names[i];
        context[arg_name] = args[i];
        dprintf("Arg_%s := `%s`\n", arg_name.c_str(), stringify(args[i]).c_str());
    }
    
    Value j = context;
    dprintf(",--- Context going into function: %s\n", stringify(j).c_str());

    std::vector<Value> _body = func;
    _body.erase(_body.begin());

    Array body = _body;
    Value res = process_function(context, body, nullptr);

    j = context;
    dprintf("'--- Context coming out of function: %s\n", stringify(j).c_str());
    return res;
}

bool is_native_function(Array& j) {
    if (j.size() < 1) {
        return false;
    }
    Value v = j[0];
    if (!v.isString()) {
        return false;
    }
    return true;
}

bool is_vm_function(Array& j) {
    if (j.size() < 2) {
        return false;
    }
    Value v = j[0];
    if (!v.isArray()) {
        return false;
    }
    return true;
}

Value function_call(context_t& context, Array& func, Array& args, bool collected) {
    dprintf("IN FUNCTION CALL\n");
    context_t func_context = context_t(context);
    //printf("- New context: %s\n", 
    dprintf("FUNCTION CALL %s\n", stringify(func).c_str());

    if (is_native_function(func)) {
        return call_native(func_context, func[0], args, collected);
    }

    if (!is_vm_function(func)) {
        eprintf("Invalid function `%s`\n", stringify(func).c_str());
        return nullptr;
    }

    if (!collected) {
        args = collect_args(func_context, args);
    }
    return call_vm(func_context, func, args);
}

Value process_array(context_t& context, Array& list) {
    if (list.size() == 0) {
        return nullptr;
    }
    // get first entry
    Value first = list[0];
    if (first.isArray()) {
        if (list.size() > 1) {
            dprintf("noticed func %s \n", stringify(first).c_str());
            return list;
        } else {
            return first;
        }
    }

    // Function call
    Value func = process_entry(context, first, false);
    dprintf("Function call `%s`\n", stringify(func).c_str());
    if (!func.isArray()) {
        eprintf("Not a function `%s`\n", stringify(func).c_str());
        return nullptr;
    }


    Array args = a_slice_i(list, 1);

    dprintf("About to cast to function\n");
    Array func_a = func;
    dprintf("About to call funciton_call\n");
    return function_call(context, func_a, args);
}

Value process_entry(context_t& context, Value& entry, bool do_assign) {
    string output = "";

    dprintf(">> Processing `%s`\n", stringify(entry).c_str());

    Value res = nullptr;

    if (entry.isString()) {
        res = process_string(context, entry);
    }
    else if (entry.isObject()) {
        Object o = entry;
        if (do_assign) {
            res = process_assign(context, o);
        } else {
            res = process_object(context, o);
        }
    }
    else if (entry.isArray()) {
        Array a = entry;
        res = process_array(context, a);
    }

    dprintf("<< Processed `%s` to `%s`\n", stringify(entry).c_str(), stringify(res).c_str());

    return res;
}


void make_args(Object& args, std::map<string,string>& res) {
    for (auto& it : args) {
        string key = it.first;
        string value = it.second;
        res[key] = value;
    }
}

string handle_view_request(Value _req, bool do_respond) {
    Value req = _req;

    string path = req["path"];

    g_sandbox = false;
    Value pargs = get_or_null(req, "args");
    dprintf("GOT args: %s\n", stringify(pargs).c_str());

    string page_text;
    string v = Php::call("__view_page", path, pargs);
    page_text = v;

    string res = "[\n"+Parse(page_text) + "\n]";
    dprintf("Formated result: %s\n", res.c_str());
    Value j = json_parse(res);

    context_t root_ctx = context_t();
    g_root_ctx = &root_ctx;
    init_runtime(root_ctx, j, g_sandbox);

    dprintf("Calling now...\n");

    Array body = j;
    process_function(root_ctx, body, &g_current_document);

    dprintf("Final result: %s\n", stringify(g_current_document).c_str());

    Object j_res;
    j_res["body"] = g_current_document;
    string css = root_ctx["__css"];
    j_res["css"] = css; 
    Value navigate = root_ctx["__navigate_target"];
    if (navigate.isObject()) {
        j_res["navigate"] = navigate;
    }
    
    string res_str = stringify(j_res);
    if (do_respond) {
        send_message(res_str);
    }
    return res_str;
}

void handle_act_request(Value req) {
    string action = req["action"];

    auto it = g_internal_actions.find(action);
    if (it == g_internal_actions.end()) {
        eprintf("Unknown action `%s`\n", action.c_str());
        return;
    }

    auto ctx = it->second.first;
    g_root_ctx = &ctx;

    auto func = it->second.second;

    Array args;
    if (req.contains("args")) {
        args = req["args"];
    }

    Value _res = function_call(ctx, func, args, true);
    Array res;
    if (_res.isArray()) {
        res = _res;
    } else {
        push_back(res, _res);
    }

    if (res.size() == 1) {
        Value first = res[0];
        if (first.isArray()) {
            res = res[0];
        }
    }

    Object j_res;
    j_res["body"] = res;
    string css = ctx["__css"];
    j_res["css"] = css; 
    Value navigate = ctx["__navigate_target"];
    if (navigate.isObject()) {
        j_res["navigate"] = navigate;
    }

    string res_str = stringify(j_res);
    send_message(res_str);
}

string get_line() {
    string line;
    getline(cin, line);
    // Remove \n from end if present
    if (line.size() > 0 && line[line.size()-1] == '\n') {
        line = line.substr(0, line.size()-1);
    }
    return line;
}

void Server_loop() {
    eprintf("Starting server loop...\n");
    Object j;
    j["status"] = "ok";
    string sj = stringify(j);
    send_message(sj);

    while(1) {

        //string inp = get_line();//Php::call("__read_request");
        string inp = Php::call("__read_request");
        dprintf("Got request: `%s`\n", inp.c_str());

        if (inp == "") {
            break;
        }

        Object req = json_parse(inp);
        // Parse request blob 

        if  (req.contains("method")) {
            string method = req["method"];
            if (method == "VIEW") {
                handle_view_request(req, true);
                continue;
            } else if (method == "ACT") {
                handle_act_request(req);
                continue;
            }
        }

        Object o;
        string res_str = stringify(o);
        //puts(res_str.c_str());
        send_message(res_str);
    }
}


Php::Value Sandbox_start(Php::Parameters &params) {
    g_sandbox = true;
    return true;
}

Php::Value Php_view_request(Php::Parameters &params)
{
    g_allow_message = false;
    Object o;
    o["method"] = "VIEW";
    o["path"] = params[0];
    o["args"] = params[1];
    dprintf("Got request: `%s`\n", stringify(o).c_str());
    string res = handle_view_request(o, false);
    //string res = "hello";
    return res;
}

Php::Value Start_server(Php::Parameters &params)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    Server_loop();
    return 1;
}



extern "C" {

    /**
     *  Function that is called by PHP right after the PHP process
     *  has started, and that returns an address of an internal PHP
     *  strucure with all the details and features of your extension
     *
     *  @return void*   a pointer to an address that is understood by PHP
     */
    PHPCPP_EXPORT void *get_module() 
    {
        Init();
        // static(!) Php::Extension object that should stay in memory
        // for the entire duration of the process (that's why it's static)
        static Php::Extension extension("niml", "1.0");
        extension.add<Start_server>("NIMS_start_server");
        extension.add<Sandbox_start>("NIMS_sandbox_start");
        extension.add<Php_view_request>("NIMS_view_request");
        eprintf("~~~~~~~~~~~~~~~~~ niml extension loaded !~~~~~~~~~~~~~\n");

        // @todo    add your own functions, classes, namespaces to the extension

        // return the extension
        return extension;
    }
}

}
