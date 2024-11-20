#include <iostream>
#include <vector>
#include <array>
#include <map>
#include <stdexcept>
#include <cctype>
#include <chrono>
#include <thread>
#include "httplib.h"
#include "json.hpp"

// important info for saving cities to favorites
struct city {
    std::string name;
    std::string lat;
    std::string lon;
};

// STRING SANITIZER METHODS:

// Trims leading and trailing whitespace on a string
std::string trim(const std::string &input) {
    // Find first non-whitespace char
    size_t first = input.find_first_not_of(" \t");
    // Find last non-whitespace char
    size_t last = input.find_last_not_of(" \t");
    // If there were no matches, return an empty string. Otherwise, return substring with specified indices
    return (first == std::string::npos || last == std::string::npos) ? "" : input.substr(first, last - first + 1);
}

// Encodes strings to url-safe representations. e.g. whitespaces into %20
std::string url_encode(const std::string &input) {
    std::string encoded;
    for (char c : input) {
        // Check if the character is safe
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c; // Append safe characters as-is
        } else {
            // Append %XX for unsafe characters
            char buffer[4]; // Enough space for "%XX\0"
            snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            encoded += buffer;
        }
    }
    return encoded;
}

// Returns a sanitized and encded input
std::string sanitize_and_encode(const std::string &input) {
    std::string trimmed = trim(input);
    return url_encode(trimmed);
}

// Checks if a given string consists of only numeric characters
bool is_numeric(const std::string& str) {
    for (char c : str) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

// API HELPER METHODS:

// Returns a JSON for a specified OpenWeather API endpoint
nlohmann::json consume_openweather_api(const std::string &endpoint) {
    // host and API keys do not change across API calls
    const std::string host = "http://api.openweathermap.org";
    const std::string open_weather_api_key = "53e275dff00cdc071833823efcd1ad2c";

    // Establish HTTP client and make GET request
    httplib::Client cli(host);
    auto response = cli.Get(endpoint + open_weather_api_key);

    // Potential HTTP GET request errors
    if (!response) {
        throw std::runtime_error("HTTPlib error: Failed to conect to OpenWeather API.");
    }
    if (response->status != 200) {
        throw std::runtime_error("HTTPlib erorr: GET requst status: " + std::to_string(response->status));
        
    }

    // Try and parse HTTP response
    try {
        nlohmann::json response_json = nlohmann::json::parse(response->body);
        return response_json;
    }
    catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse response body to JSON: " + std::string(e.what()));
    }
}

// Given lat and lon coords, return an ordered map of that location's weather using consume_openweather_api helper
std::map<std::string, double> get_weather_from_lat_lon(const std::string &lat, const std::string &lon) {
    // builds parts of endpoint url
    const std::string weather_url_prefix = "/data/2.5/weather?";
    const std::string weather_url_suffix = "&units=metric&appid=";

    try {
        nlohmann::json weather_res_json = consume_openweather_api(weather_url_prefix + "lat=" + lat + "&lon=" + lon + weather_url_suffix);

        // If API response is missing information or failed to get weather info
        if (weather_res_json.empty()) {
            throw std::runtime_error("API returned empty result.");
        }
        if (weather_res_json["cod"] != 200) {
            throw std::runtime_error("API returned error: " + std::string(weather_res_json["message"]));
        }

        // populate map with returned JSON data
        std::map<std::string, double> weather_data;
        double temp = weather_res_json["main"]["temp"];
        weather_data["Temperature (Celcius)"] = temp;

        double feels_like = weather_res_json["main"]["feels_like"];
        weather_data["Feels Like (Celcius)"] = feels_like;

        double pressure = weather_res_json["main"]["pressure"];
        weather_data["Pressure (hPa)"] = pressure;

        double humidity = weather_res_json["main"]["humidity"];
        weather_data["Humidity(%)"] = humidity;

        double temp_min = weather_res_json["main"]["temp_min"];
        weather_data["Min Temperature (Celcius)"] = temp_min;

        double temp_max = weather_res_json["main"]["temp_max"];
        weather_data["Max Temperature (Celcius)"] = temp_max;

        double wind_speed = weather_res_json["wind"]["speed"];
        weather_data["Wind Speed (meters/sec)"] = wind_speed;

        double cloudiness = weather_res_json["clouds"]["all"];
        weather_data["Cloudiness (%)"] = cloudiness;

        if (weather_res_json.contains("rain")) {
            double rain = weather_res_json["rain"]["1h"];
            weather_data["Rain (mm/hr)"] = rain;
        }
        else {
            weather_data["Rain (mm/hr)"] = 0.0;
        }
        
        if (weather_res_json.contains("snow")) {
            double snow = weather_res_json["snow"]["1h"];
            weather_data["Snow (mm/hr)"] = snow;
        }
        else {
            weather_data["Snow (mm/hr)"] = 0.0;
        }
        return weather_data;
    }
    // will catch any exceptions that have percolated up from previous function calls
    catch (const std::exception& e) {
        throw std::runtime_error("Error fetching weather data: " + std::string(e.what()));
    }
}

// Given city name, sanitize and encodes the name then returns city's lat and lon using consume_openweather_api helper
std::pair<std::string, std::string> get_city_lat_lon(const std::string &city_name) {
     // builds parts of endpoint url
    const std::string geo_url_prefix = "/geo/1.0/direct?q=";
    const std::string geo_url_suffix = "&limit=1&appid="; 

    try {
        nlohmann::json geo_res_json = consume_openweather_api(geo_url_prefix + sanitize_and_encode(city_name) + geo_url_suffix)[0];
        
        // If API response is missing information
        if (geo_res_json.empty()) { 
            throw std::runtime_error("API returned empty result.");
        }
        if (!geo_res_json.contains("lat") || !geo_res_json.contains("lon")) {
            throw std::runtime_error("API missing lat or lon information.");
        }

        // populate the lat and lon with returned JSON data
        std::pair<std::string, std::string> lat_lon_coords;
        double lat_raw = geo_res_json["lat"];
        double lon_raw = geo_res_json["lon"];
        lat_lon_coords = std::make_pair(std::to_string(lat_raw), std::to_string(lon_raw));
        return lat_lon_coords;
    }
    // will catch any exceptions that have percolated up from previous function calls
    catch (const std::exception& e) {
        throw std::runtime_error("Error fetching geocoding data: " + std::string(e.what()));
    }
}

// DISPLAY METHODS:

// display weather information to the terminal for a given city
void city_search() {
    std::cout << "====================== City Search ======================" << std::endl;
    std::cout << "Enter the name of the city or '-1' to go back to the main screen.\n";
    std::cout << "City Name: ";

    // Get user input
    std::string city_name;
    getline(std::cin, city_name);
    if (city_name == "-1") {
        return;
    }

    try {
        // user helper functions
        std::pair<std::string, std::string> lat_lon_coords = get_city_lat_lon(city_name);
        std::map<std::string, double> weather_data = get_weather_from_lat_lon(lat_lon_coords.first, lat_lon_coords.second);

        std::cout << "Weather Data for " << city_name << ":\n";
        for (const auto& kv_pair : weather_data) {
            std::cout << kv_pair.first << ": " << kv_pair.second << "\n";
        }
        std::cout << std::endl;
    } 
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;;
    }
}

// display user's favorites and give option to delete one
void delete_favorite(std::vector<city> &favorites) {
    std::cout << "====================== Delete City ======================" << std::endl;
    if (favorites.empty()) {
        std::cout << "No favorite cities to delete" << std::endl;
        return;
    }

    // display favorites
    std::cout << "Current favorite cities: " << std::endl;
    int i = 1;
    for (const auto &favorite : favorites) {
        std::cout << i << ". " << favorite.name << "\n";
        i++;
    }

    std::cout << "Please type the number of the city you wish to delete, or press '-1' to go back to the main screen.\n";
    std::cout << "City Number: ";

    // Get user input
    std::string choice;
    getline(std::cin, choice);
    if (choice == "-1") {
        return;
    }

    // check if user input is a valid index
    if (is_numeric(choice)) {
        // convert string to integer index
        int index = std::stoi(choice);
        if (index > 0 && index <= favorites.size()) {
            favorites.erase(favorites.begin() + index - 1);
            std::cout << "City successfully deleted." << std::endl;
        } else {
            std::cout << "Number out of bounds. Please try again." << std::endl;
        }
    } else {
        std::cerr << "Invalid input. Please enter a valid number." << std::endl;
    }
}

// display user's favorites and give option to add one
void add_favorite(std::vector<city> &favorites) {
    std::cout << "====================== Add City ======================" << std::endl;
    // display favorites
    std::cout << "Current favorite cities: " << std::endl;
    int i = 1;
    for (const auto &favorite : favorites) {
        std::cout << i << ". " << favorite.name << "\n";
        i++;
    }

    std::cout << "Please type the name of the city you wish to add, or press '-1' to go back to the main screen.\n";
    std::cout << "City Name: ";

    // Get user input
    std::string city_name;
    getline(std::cin, city_name);
    if (city_name == "-1") {
        return;
    }

    // check if the favorites list is full
    if (favorites.size() < 3) {
        try {
            // use helper function to get city coords
            std::pair<std::string, std::string> lat_lon_coords = get_city_lat_lon(city_name);
            // add city name, lat, and lon to city data structure
            city fav;
            fav.name = city_name;
            fav.lat = lat_lon_coords.first;
            fav.lon = lat_lon_coords.second;
            favorites.push_back(fav);
            std::cout << "Favorite successfully added: " << city_name << std::endl;
        }
        catch(const std::exception& e) {
            std::cerr << "Error adding favorite: " << e.what() << std::endl;;
        }
    }
    else {
        std::cout << "Cannot add city: Favorites list is full." << std::endl;
    }
}

// display user's favorite cities and their respective weather
void display_favorites(const std::vector<city> &favorites) {
    std::cout << "====================== Favorite Cities ======================" << std::endl;
    if (favorites.empty()) {
        std::cout << "No favorite cities to display." << std::endl;
        return;
    }

    for (const auto &favorite : favorites) {
        std::cout << "City Name: " << favorite.name << std::endl;
        try {
            std::map<std::string, double> weather_data = get_weather_from_lat_lon(favorite.lat, favorite.lon); 
            for (const auto &kv_pair : weather_data) {
                std::cout << kv_pair.first << ": " << kv_pair.second << "\n";
            }
            std::cout << std::endl;
        }
        catch(const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
}

// Main screen to select which of this app's functions to execute
// After each function is executed, go back to main screen
void main_screen(std::vector<city> &favorites, bool &exit_flag) {
    std::cout << "====================== Main Screen ======================" << std::endl;
    std::cout << "Hello, welcome to my application.\n\n";
    std::cout << "Please enter a number corresponding to an action below.\n";
    std::cout << "1. Search for a city's weather.\n";
    std::cout << "2. Add to your favorite cities.\n";
    std::cout << "3. Delete from your favorite cities.\n";
    std::cout << "4. View weather of your favorite cities.\n";
    std::cout << "5. Exit program."  << std::endl;

    std::cout << "Enter '1', '2', '3', '4', or '5': ";
    std::string choice;
    getline(std::cin, choice);
    if (choice == "1") {
        city_search();
    }
    else if (choice == "2") {
        add_favorite(favorites);
    }
    else if (choice == "3") {
        delete_favorite(favorites);
    }
    else if (choice == "4") {
        display_favorites(favorites);
    }
    else if (choice == "5") {
        exit_flag = true;
    }
    else {
        std::cout << "Invalid choice, please try again." << std::endl;
    }
}

// applications main entry point
int main() {
    std::vector<city> favorites;
    bool exit_flag = false;
    while (!exit_flag) {
        main_screen(favorites, exit_flag);
        // sleep for half a second so user can better see CLI navigation
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    std::cout << "Exiting program." << std::endl;
    return 0;
}