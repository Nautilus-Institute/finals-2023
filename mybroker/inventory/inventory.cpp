#include "crow.h"
#include "crow/middleware.h"
#include "sqlite_modern_cpp.h"

#define DATABASE "database.db"

using namespace crow;

int main()
{
    char* database = getenv("DATABASE");
    if (!database) {
        database = DATABASE;
    }
    char* port_str = getenv("INVENTORY_PORT");
    if (!port_str) {
        port_str = "8900";
    }
    int port = atoi(port_str);

    sqlite::database db(database);

    SimpleApp app;

    // GET: get inventory
    CROW_ROUTE(app, "/api/v1/inventory/list")
    .methods("GET"_method)
    ([database](const request& req){
        json::wvalue x;
        int i = 0;

        try {
            sqlite::database db(database);

            db << "SELECT id, address, bedrooms, bathrooms, price FROM house;" >> [&](int id, std::string address, int bedrooms, int bathrooms, int price) {
                x[i]["id"] = id;
                x[i]["address"] = address;
                x[i]["bedrooms"] = bedrooms;
                x[i]["bathrooms"] = bathrooms;
                x[i]["price"] = price;
                i++;
            };
        } catch (sqlite::sqlite_exception &ex){
            std::cerr << ex.what() << std::endl << ex.get_sql() << std::endl;
            return response(500);
        }
        return response(x);
    });

    // POST: add a new house
    CROW_ROUTE(app, "/api/v1/inventory/new")
    .methods("POST"_method)
    ([database](const request& req){
        auto x = json::load(req.body);

        if (!x) {
            return response(400);
        }

        std::string address = x["address"].s();
        int bedrooms = x["bedrooms"].i();
        int bathrooms = x["bathrooms"].i();
        int price = x["price"].i();

        try {
            sqlite::database db(database);

            db << "INSERT INTO house (address, bedrooms, bathrooms, price) VALUES (?,?,?,?);"
               << address
               << bedrooms
               << bathrooms
               << price;
        } catch (sqlite::sqlite_exception &ex){
            std::cerr << ex.get_code() << ": " << ex.what() << std::endl << ex.get_sql() << std::endl;
            return response(500);
        }

        return response(200);
    });

    // GET: get a house
    CROW_ROUTE(app, "/api/v1/inventory/get/<int>")
    .methods("GET"_method)
    ([database](const request& req, int id){
        json::wvalue x;
        int i = 0;

        try {
            sqlite::database db(database);

            db << "SELECT id, address, bedrooms, bathrooms, price FROM house where id = ?;" << id >> [&](int id, std::string address, int bedrooms, int bathrooms, int price) {
                x[i]["id"] = id;
                x[i]["address"] = address;
                x[i]["bedrooms"] = bedrooms;
                x[i]["bathrooms"] = bathrooms;
                x[i]["price"] = price;
                i++;
            };
        } catch (sqlite::sqlite_exception &ex){
            std::cerr << ex.what() << std::endl;
            return response(500);
        }
        return response(x);
    });

    // POST: delete a house
    // VULN
    CROW_ROUTE(app, "/api/v1/inventory/<string>/<string>")
    .methods("POST"_method)
    ([database](const request& req, std::string method, std::string id){
        try {
            sqlite::database db(database);
            // Build the request
            std::ostringstream str;
            str << method
                << " FROM house WHERE id = "
                << id
                << ";";

            db << str.str();
        } catch (sqlite::sqlite_exception &ex){
            std::cerr << ex.what() << std::endl;
            return response(500);
        }
        return response(200);
    });


#ifndef DEBUG
    app.loglevel(crow::LogLevel::Warning);
#endif

    app.bindaddr("::1").port(port).multithreaded().run();

    return 0;
}

