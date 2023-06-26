#include <iostream>
#include <chrono>
#include <queue>
#include <set>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <regex>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <chrono>

#include <signal.h>
#include <gumbo.h>
#include <cpr/cpr.h>
#include <sqlite_orm/sqlite_orm.h>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/functional/hash.hpp>
#include <boost/filesystem.hpp>
#include <boost/locale.hpp>
#include <boost/regex.hpp>

#include <dlib/image_io.h>
#include <dlib/image_transforms/interpolation.h>
#include <dlib/image_transforms.h>

using namespace sqlite_orm;
namespace po = boost::program_options;

// Globals
const size_t max_image_dims = 1280;
const size_t min_image_file_size = 200;
const size_t max_image_file_size = (4 * 1024 * 1024);
const size_t urls_queue_threshold_max = 50000;
const size_t urls_queue_threshold_min = 2000;
const size_t max_threads = 100;
const size_t max_str_length = 1024;
const size_t max_url_length = 450;
const size_t max_html_page_size = (2 * 1024 * 1024);
const size_t auto_flush_time = (5 * 60);
const std::string unsupported_image_mime = "unsupported";
const std::string user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3";
std::mutex mtx;
std::atomic<size_t> total_pages = 0;
std::atomic<size_t> total_images = 0;
std::atomic<bool> verbose = false;
std::atomic<bool> no_new_urls_auto = false;
std::atomic<bool> no_new_urls = false;

class ElapsedTime {
public:
    ElapsedTime() : m_startTime(std::chrono::high_resolution_clock::now()) {}
    void reset() { m_startTime = std::chrono::high_resolution_clock::now(); }

    long long getMilliseconds() const {
        auto endTime = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(endTime - m_startTime).count();
    }
    long long getSeconds() const {
        auto endTime = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(endTime - m_startTime).count();
    }

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
};

// To properly stop the program
std::atomic<bool> stop_requested(false);
BOOL CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
    case CTRL_C_EVENT:
        // handle CTRL+C event here
        stop_requested.store(true);
        if (verbose) std::cout << std::endl << "Stopping the current crawling process. Exiting..." << std::endl;
        return TRUE;
    default:
        return FALSE;
    }
}

// Url metadata struct
struct UrlData {    
    std::string url;
    std::unique_ptr <std::string> last_crawled;
    std::string last_seen;
    std::size_t status_code;
};

// Image metadata struct
struct ImageData {
    std::string url;
    std::unique_ptr<std::string> alt;
    std::string source_page;
    std::unique_ptr<std::string> surrounding_text;
    std::size_t file_size;
    std::size_t width;
    std::size_t height;
    std::unique_ptr<std::string> mime;
    std::string last_seen;
};

// Database connection and table mapping
auto storage = make_storage("queues.db",
    sqlite_orm::make_table("images",
        make_column("url", &ImageData::url, unique()),
        make_column("alt", &ImageData::alt),
        make_column("source", &ImageData::source_page),
        make_column("surrounding", &ImageData::surrounding_text),
        make_column("file_size", &ImageData::file_size),
        make_column("width", &ImageData::width),
        make_column("height", &ImageData::height),
        make_column("mime", &ImageData::mime),
        make_column("last_seen", &ImageData::last_seen)),
    sqlite_orm::make_table("urls",
        make_column("url", &UrlData::url, unique()),
        make_column("last_crawled", &UrlData::last_crawled),
        make_column("last_seen", &UrlData::last_seen),
        make_column("status_code", &UrlData::status_code))
);
// In memory structure
auto memory_storage = make_storage(":memory:",
    make_table("images",
        make_column("url", &ImageData::url, unique()),
        make_column("alt", &ImageData::alt),
        make_column("source", &ImageData::source_page),
        make_column("surrounding", &ImageData::surrounding_text),
        make_column("file_size", &ImageData::file_size),
        make_column("width", &ImageData::width),
        make_column("height", &ImageData::height),
        make_column("mime", &ImageData::mime),
        make_column("last_seen", &ImageData::last_seen)),
    make_table("urls",
        make_column("url", &UrlData::url, unique()),
        make_column("last_crawled", &UrlData::last_crawled),
        make_column("last_seen", &UrlData::last_seen),
        make_column("status_code", &UrlData::status_code))
);

std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t time_now = std::chrono::system_clock::to_time_t(now);

    struct tm tm;
    localtime_s(&tm, &time_now);

    std::stringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string remove_spaces(const std::string& str) {
    // Use regex_replace to replace all occurrences of the pattern with a single space
    return boost::regex_replace(str, boost::regex(std::string("\\s+")), " ");
}
// Common stop words in English, French, German, Spanish, and Italian
std::unordered_set<std::string> stop_words = {
    // English
    "a", "an", "the", "and", "but", "or", "if", "while", "of", "at", "by",
    "for", "with", "about", "against", "between", "into", "through", "during",
    "before", "after", "above", "below", "to", "from", "in", "out", "on", "off",
    "over", "under", "again", "further", "then", "once", "here", "there", "when",
    "where", "why", "how", "all", "any", "both", "each", "few", "more", "most",
    "other", "some", "such", "no", "nor", "not", "only", "own", "same", "so",
    "than", "too", "very", "can", "will", "just", "don", "should", "now", "com",
    // French
    "je", "tu", "il", "elle", "nous", "vous", "ils", "elles",
    "le", "la", "les", "un", "une", "des", "fr",
    // German
    "der", "die", "das", "ein", "eine", "eines", "einem", "einen", "de",
    // Spanish
    "el", "la", "los", "las", "un", "una", "unos", "unas", "es",
    // Italian
    "di", "che", "e", "la", "il", "un", "una", "per", "in", "con", "su",
    "da", "del", "della", "dello", "dei", "degli", "delle", "al", "dal", "dalla",
    "dai", "dagli", "alle", "col", "sul", "sull", "sulla", "sullo", "sui", "sugli",
    "sulle", "nei", "negli", "nelle", "perché", "così", "quindi", "allora", "anche",
    "come", "dove", "quando", "chi", "non", "mai", "più", "meno", "tuttavia",
    "ovunque", "altrove", "addirittura", "sempre", "già", "appena", "proprio",
    "nient", "altro", "nulla", "qualcosa", "qualcuno", "tutt", "solamente", "it"
};
boost::locale::generator gen;
std::locale loc = gen("");
std::string replace_non_iso_ascii_chars(const std::string& input) {
    std::string output;
    for (const auto& ch : input) {
        if (ch < 0 || ch > 127) {
            std::string str_ch(1, ch);            
            output += boost::locale::fold_case(boost::locale::normalize(str_ch, boost::locale::norm_nfd), loc);
        }
        else {
            output += ch;
        }
    }
    return output;
}
void remove_stop_words(std::string& input) {
    std::string stripped = replace_non_iso_ascii_chars(input);
    std::vector<std::string> words;
    boost::split(words, stripped, boost::is_any_of(" .,:;!?#@[]{}|\"&")); // Split the input string into individual words
    words.erase(std::remove_if(words.begin(), words.end(),
        [](const std::string& word) {
            return stop_words.find(word) != stop_words.end();
        }), words.end()); // Erase stop words from the vector
    input = remove_spaces(boost::join(words, " ")); // Join the remaining words back into a string
}

std::string get_abs_url(const std::string& link, const std::string& base_url, const bool for_image) {
    std::string abs_url("");
    std::regex js_regex("(javascript:|data:image/|mailto:)", std::regex_constants::icase);
    if (std::regex_search(link, js_regex)) return abs_url;

    // Check if the link is already an absolute URL
    std::regex http_regex("^https?://", std::regex_constants::icase);
    if (std::regex_search(link, http_regex)) return link;

    // Check if the link starts with a slash, indicating a relative URL    
    if (link[0] == '/') {
        std::regex base_http_regex("^https?://[^/]+", std::regex_constants::icase);
        std::smatch base_match;
        if (std::regex_search(base_url, base_match, base_http_regex)) {
            std::string base_domain = base_match[0];
            abs_url = base_domain + link;
        }
    }
    else { // Otherwise, assume the link is a relative URL
        std::string base_dir = base_url.substr(0, base_url.find_last_of('/') + 1);
        abs_url = base_dir + link;

        // Test if the link starts with a domain name
        std::regex domain_regex("^[^/]+\\.(com|org|net|edu|gov|mil|biz|info|name|museum|us|ca|uk|fr|de|jp|ru|cn|es|it|au|nz|ch|nl|be|se|no|fi|dk|at|gr|ie|pl|pt|cz|ro|hu|sk|hr|bg|rs|lv|lt|ee|is|cy|lu|mt|md|al|ad|li|mc|sm|va|by|ua|kz|uz|tm|kg|ge|am|az|tr|il|in|ae|sa|ir|kw|bh|qa|om|ye|ps|lb|jo|sy|iq|eg|ly|dz|ma|tn|sd|er|so|ke|et|dj|ug|bi|rw|mg|mu|sc|za|na|bw|zw|zm|sz|ls|mw|sz|gq|ga|st|cv|td|km|so|cg|ci|lr|sl|gh|ng|cm|cf|tn|mr|sn|gn|gw|tg|bf|ne|mg|mu|re|yt|tf|nf|aq|hm|bv|gs)$", std::regex_constants::icase);
        if (std::regex_search(link, domain_regex)) {
            // Prepend the protocol to the domain name
            std::regex base_http_regex("^https?://", std::regex_constants::icase);
            std::smatch base_match;
            if (std::regex_search(base_url, base_match, base_http_regex)) {
                std::string protocol = base_match[0];
                abs_url = protocol + link;
            }
        }        
    }
    // Remove any query string or fragment identifier
    abs_url = abs_url.substr(0, abs_url.find_first_of(for_image ? "?#" : "#"));
    if (abs_url.back() == '/') abs_url.pop_back();
    boost::replace_all(abs_url, " ", "%20");
    return (abs_url.size() < max_url_length ? abs_url : "");
}

std::string get_first_h1_text(const GumboNode* root) {
    std::string text = "";
    if (root != nullptr && root->type == GUMBO_NODE_ELEMENT) {
        if (root->v.element.tag == GUMBO_TAG_H1) {
            const GumboVector* children = &root->v.element.children;
            if (children != nullptr && children->length > 0) {
                const GumboNode* child = static_cast<const GumboNode*>(children->data[0]);
                if (child != nullptr && child->type == GUMBO_NODE_TEXT && child->v.text.text != nullptr) text = child->v.text.text;
            }
        }
        else {
            const GumboVector* children = &root->v.element.children;
            if (children != nullptr) {
                for (unsigned int i = 0; i < children->length; ++i) {
                    const GumboNode* text_node = static_cast<const GumboNode*>(children->data[i]);
                    if (text_node != nullptr) {
                        const std::string child_text = get_first_h1_text(text_node);
                        if (!child_text.empty()) {
                            text = child_text;
                            break;
                        }
                    }
                }
            }
        }
    }
    return text;
}

std::string get_page_title(const GumboNode* root) {
    std::string title = "";
    bool found_head = false;
    const GumboVector* children = &root->v.element.children;
    if (children == nullptr) return title;
    for (unsigned int i = 0; i < children->length; ++i) {
        const GumboNode* child = static_cast<const GumboNode*>(children->data[i]);
        if (child != nullptr && child->type == GUMBO_NODE_ELEMENT && child->v.element.tag == GUMBO_TAG_HEAD) {
            found_head = true;
            const GumboVector* head_children = &child->v.element.children;
            if (head_children != nullptr) {
                for (unsigned int j = 0; j < head_children->length; ++j) {
                    const GumboNode* title_node = static_cast<const GumboNode*>(head_children->data[j]);
                    if (title_node != nullptr && title_node->type == GUMBO_NODE_ELEMENT && title_node->v.element.tag == GUMBO_TAG_TITLE) {
                        const GumboVector* title_children = &title_node->v.element.children;
                        if (title_children != nullptr && title_children->length > 0) {
                            const GumboNode* text_node = static_cast<const GumboNode*>(title_children->data[0]);
                            if (text_node != nullptr && text_node->type == GUMBO_NODE_TEXT && text_node->v.text.text != nullptr) {
                                title = text_node->v.text.text;
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (found_head) break;
    }
    return title;
}

// Helper function to extract image links from a page and download source images
std::string calculate_md5_from_path(boost::filesystem::path& p) {
    std::string file_name = p.filename().string();
    std::string sub_folder_name = p.parent_path().filename().string();
    std::string top_folder_name = p.parent_path().parent_path().filename().string();
    return (top_folder_name + sub_folder_name + file_name.substr(0, file_name.find_last_of('.')));
}
void get_file_folder(const std::string& md5_hash, std::string& folder_pathname) {
    // Split the MD5 hash into two parts: the first 2 characters and the remaining characters
    std::string top_folder_name = md5_hash.substr(0, 1);
    std::string sub_folder_name = md5_hash.substr(1, 1);
    std::string file_name = md5_hash.substr(2);

    // Construct the destination folder path based on the top folder name and subfolder name
    folder_pathname = boost::filesystem::current_path().string() + "/img_cache";
    if (!boost::filesystem::exists(folder_pathname)) boost::filesystem::create_directory(folder_pathname);
    folder_pathname += "/" + top_folder_name;
    if (!boost::filesystem::exists(folder_pathname)) boost::filesystem::create_directory(folder_pathname);
    folder_pathname += "/" + sub_folder_name;
    if (!boost::filesystem::exists(folder_pathname)) boost::filesystem::create_directory(folder_pathname);
    folder_pathname += "/" + file_name + ".jpg";
}
bool download_image(cpr::Session& session, const std::string& url, const std::string& filename, size_t& file_size, size_t& width, size_t& height, std::string& file_type) {            
    auto ouput_file = std::ofstream(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!ouput_file.is_open()) return false;
    try {
        session.SetUrl(cpr::Url{ url });
        auto r = session.Download(ouput_file);
        ouput_file.flush(); // Flush data on-disk before continuing
        ouput_file.close();
        if (r.status_code != 200) {
            if (verbose) std::cerr << "Error downloading image from " << url << " - " << r.status_code << " " << r.error.message << std::endl;
            boost::filesystem::remove(filename);
            return false;
        }
    }
    catch (...) {
        ouput_file.flush(); // Flush data on-disk before continuing
        ouput_file.close();
        if (verbose) std::cerr << "Error downloading image from " << url << std::endl;
        boost::filesystem::remove(filename);
        return false;
    }

    // Get image file size in bytes, also check its type
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file) {
        if (verbose) std::cerr << "Error opening image file: " << filename << std::endl;
        boost::filesystem::remove(filename);
        return false;
    }
    file.seekg(0, std::ios::end);
    file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (file_size < min_image_file_size || file_size > max_image_file_size) {
        file.close();
        if (verbose) std::cerr << "Too small or large image file: " << filename << " (" << file_size << " bytes)" << std::endl;
        boost::filesystem::remove(filename);
        return false;
    }
    std::vector<char> data(std::min<std::size_t>(file_size, 10));
    if (!file.read(&data[0], data.size())) {
        file.close();
        if (verbose) std::cerr << "Error reading image file: " << filename << std::endl;
        boost::filesystem::remove(filename);
        return false;
    }
    file.close();
    if (data[0] == (char)0xFF && data[1] == (char)0xD8) file_type = "jpg";
    /* else if (data[0] == (char)0x47 && data[1] == (char)0x49 && data[2] == (char)0x46) file_type = "gif"; */
    else if (data[0] == (char)0x89 && data[1] == (char)0x50 && data[2] == (char)0x4E && data[3] == (char)0x47 &&
        data[4] == (char)0x0D && data[5] == (char)0x0A && data[6] == (char)0x1A && data[7] == (char)0x0A) file_type = "png";
    else {
        if (verbose) std::cerr << "Unsupported file type for image " << url << std::endl;
        boost::filesystem::remove(filename);
        return false;
    }

    // Extract real dimensions and resize if required
    try {        
        dlib::array2d<dlib::rgb_pixel> img;
        dlib::load_image(img, filename);
        width = img.nc();
        height = img.nr();
        if (width > max_image_dims || height > max_image_dims) {
            const double resize_factor = std::min(max_image_dims / (double)width, max_image_dims / (double)height);
            const size_t new_width = static_cast<size_t>(width * resize_factor);
            const size_t new_height = static_cast<size_t>(height * resize_factor);
            dlib::array2d<dlib::rgb_pixel> size_img(new_height, new_width);
            dlib::resize_image(img, size_img);
            dlib::save_jpeg(size_img, filename, 90);
        }
        else {
            dlib::save_jpeg(img, filename, 90);
        }
    }
    catch (std::exception& e) {
        if (verbose) std::cerr << "Error processing image file: " << filename << " - " << e.what() << std::endl;
        boost::filesystem::remove(filename);
        return false;
    }

    return true;
}

void extract_image_links(const GumboNode* root_node, const std::string& base_url, const std::string& title) {
    cpr::Session session;
    session.SetUserAgent(cpr::UserAgent{ user_agent });
    session.SetHeader(cpr::Header{ {"Accept", "image/png, image/jpeg"} });
    session.SetConnectTimeout(cpr::ConnectTimeout{ 2500 });
    session.SetTimeout(cpr::Timeout{ 8500 });

    std::vector<GumboNode*> nodes;
    nodes.push_back((GumboNode*)root_node);

    while (!nodes.empty()) {
        GumboNode* node = nodes.back();
        nodes.pop_back();
        if (node == nullptr || (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_DOCUMENT)) continue;
        if (node->v.element.tag == GUMBO_TAG_IMG) {
            GumboAttribute* src_attr = gumbo_get_attribute(&node->v.element.attributes, "src");
            GumboAttribute* alt_attr = gumbo_get_attribute(&node->v.element.attributes, "alt");
            if (src_attr) {
                std::string src_url = get_abs_url(src_attr->value, base_url, true), surrounding("");
                if (src_url.find("http") == 0) {  // Check if absolute URL                    
                    std::string alt = alt_attr ? alt_attr->value : std::string("");
                    if (!alt.empty()) {
                        remove_stop_words(alt);
                        if(alt.length() > max_str_length) {
                            size_t space_pos = alt.find(' ', max_str_length);
                            if (space_pos != std::string::npos) alt = alt.substr(0, space_pos);
                            else alt = alt.substr(0, max_str_length);
                        }
                        boost::algorithm::trim(alt);
                    }

                    // Collect text nodes before and after the image
                    GumboNode* parent = node->parent;
                    if (parent != nullptr && parent->type == GUMBO_NODE_ELEMENT) {
                        GumboVector* children = &parent->v.element.children;
                        if (children != nullptr) {
                            int j;
                            for (unsigned int i = 0; i < children->length; ++i) {
                                if (static_cast<GumboNode*>(children->data[i]) == node) { // This is the image node, skip it                                    
                                    j = i - 1;
                                    while (j > 0) {
                                        if ((static_cast<GumboNode*>(children->data[j])) != nullptr && (static_cast<GumboNode*>(children->data[j]))->type == GUMBO_NODE_TEXT && (static_cast<GumboNode*>(children->data[j]))->v.text.text != nullptr) {
                                            surrounding = (surrounding.empty() ? "" : " ") + std::string((static_cast<GumboNode*>(children->data[j]))->v.text.text);
                                            break;
                                        }
                                        j--;
                                    }                                    
                                    j = i + 1;
                                    while (j < static_cast<int>(children->length)) {
                                        if ((static_cast<GumboNode*>(children->data[j])) != nullptr && (static_cast<GumboNode*>(children->data[j]))->type == GUMBO_NODE_TEXT && (static_cast<GumboNode*>(children->data[j]))->v.text.text != nullptr) {
                                            surrounding = (surrounding.empty() ? "" : " ") + std::string((static_cast<GumboNode*>(children->data[j]))->v.text.text);
                                            break;
                                        }
                                        j++;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    if (!title.empty()) surrounding = title + " " + surrounding;                    
                    if (!surrounding.empty()) remove_stop_words(surrounding);
                    if (surrounding.length() > max_str_length) {
                        size_t space_pos = surrounding.find(' ', max_str_length);
                        if (space_pos != std::string::npos) {
                            surrounding = surrounding.substr(0, space_pos);
                        }
                        else {
                            surrounding = surrounding.substr(0, max_str_length);
                        }
                    }
                    boost::algorithm::trim(surrounding);
                    std::string last_seen = get_current_time();

                    size_t file_size = 0, width = 0, height = 0;
                    std::string mime("");
                    
                    ImageData data{ src_url, std::make_unique<std::string>(""), base_url, std::make_unique<std::string>(""), file_size, width, height, std::make_unique<std::string>(mime), last_seen};
                    bool is_new_image = false;
                    std::unique_lock<std::mutex> lck(mtx);
                    try {
                        memory_storage.insert(data);
                        total_images++;
                        if (verbose) std::cout << "url: " << src_url << " - src: " << base_url << " - alt: " << alt << " - surrounding: " << surrounding << " - last_seen: " << last_seen << std::endl;
                        is_new_image = true;
                    }
                    catch (std::system_error e) {
                        std::string error_msg = e.what();
                        if (error_msg.find("UNIQUE constraint failed") != std::string::npos) {
                            memory_storage.update_all(set(c(&ImageData::last_seen) = last_seen),
                                where(c(&ImageData::url) == src_url));
                         }
                         else {
                             if (verbose) std::cout << error_msg << std::endl;
                         }
                    }
                    catch (...) {
                        if (verbose) std::cout << "unknown exeption" << std::endl;
                    }
                    lck.unlock();
                    if (is_new_image) { // Only download image if not already present into the database                        
                        size_t md5_as_int = boost::hash<std::string>{}(src_url); // Calculate the MD5 hash of the string
                        std::stringstream ss;
                        ss << std::hex << std::setw(16) << std::setfill('0') << md5_as_int;
                        std::string filename, md5_as_str = ss.str();
                        get_file_folder(md5_as_str, filename);

                        if (download_image(session, src_url, filename, file_size, width, height, mime)) {
                            lck.lock();                            
                            memory_storage.update_all(set(c(&ImageData::alt) = std::make_unique<std::string>(alt),
                                c(&ImageData::surrounding_text) = std::make_unique<std::string>(surrounding),
                                c(&ImageData::file_size) = file_size,
                                c(&ImageData::width) = width,
                                c(&ImageData::height) = height,
                                c(&ImageData::mime) = std::make_unique<std::string>(mime)),
                                where(c(&ImageData::url) == src_url));                            
                            lck.unlock();
                        }
                        else {
                            lck.lock();
                            memory_storage.update_all(set(c(&ImageData::mime) = unsupported_image_mime), where(c(&ImageData::url) == src_url));
                            lck.unlock();
                        }
                    }
                }
            }
        }
        // Add all child nodes to the vector
        GumboVector* children = &node->v.element.children;
        if (children != nullptr) {
            for (unsigned int i = 0; i < children->length; ++i) {
                nodes.push_back(static_cast<GumboNode*>(children->data[i]));
            }
        }
    }
}

void extract_links(const GumboNode* root_node, const std::string& base_url) {
    std::vector<GumboNode*> nodes;
    nodes.push_back((GumboNode*)root_node);

    while (!nodes.empty()) {
        GumboNode* node = nodes.back();
        nodes.pop_back();
        if (node == nullptr || node->type != GUMBO_NODE_ELEMENT) continue;
        if (node->v.element.tag == GUMBO_TAG_A) {
            GumboAttribute* href = gumbo_get_attribute(&node->v.element.attributes, "href");
            if (href != nullptr) {
                std::string link = href->value;
                boost::algorithm::trim(link);
                if (!link.empty()) {
                    std::string abs_url = get_abs_url(link, base_url, false);
                    std::string last_crawled(""), last_seen = get_current_time();

                    UrlData data{ abs_url, std::make_unique<std::string>(last_crawled), last_seen, 100 };
                    std::unique_lock<std::mutex> lck(mtx);
                    try {
                        memory_storage.insert(data);
                        if (verbose) std::cout << "url: " << abs_url << " - last_seen: " << last_seen << std::endl;
                    }
                    catch (std::system_error e) {
                        std::string error_msg = e.what();
                        if (error_msg.find("UNIQUE constraint failed") != std::string::npos) {
                            memory_storage.update_all(set(c(&UrlData::last_seen) = last_seen),
                                where(c(&UrlData::url) == abs_url));
                        }
                        else {
                            if (verbose) std::cout << error_msg << std::endl;
                        }
                    }
                    catch (...) {
                        if (verbose) std::cout << "unknown exeption" << std::endl;
                    }
                    lck.unlock();
                }
            }
        }
        // Add all child nodes to the vector
        GumboVector* children = &node->v.element.children;
        if (children != nullptr) {
            for (unsigned int i = 0; i < children->length; ++i) {
                nodes.push_back(static_cast<GumboNode*>(children->data[i]));
            }
        }
    }
}

// Spider function that crawls URLs
void spider() {
    cpr::Session session;
    session.SetUserAgent(cpr::UserAgent{ user_agent });
    session.SetHeader(cpr::Header{ {"Accept", "text/*"} });
    session.SetConnectTimeout(cpr::ConnectTimeout{ 2500 });
    session.SetTimeout(cpr::Timeout{ 5500 });

    while (!stop_requested) {        
        std::string url("");        
        std::unique_lock<std::mutex> lck(mtx);
        auto limited1 = memory_storage.get_all<UrlData>(where(c(&UrlData::last_crawled) == ""), limit(1));
        if (limited1.size() == 1) {
            url = limited1[0].url;
            memory_storage.update_all(set(c(&UrlData::last_crawled) = get_current_time(), c(&UrlData::status_code) = 200), where(c(&UrlData::url) == url));
        }
        lck.unlock();
        if (url.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }
                        
        session.SetUrl(cpr::Url{ url });        
        GumboOutput* doc = nullptr;
        try {
            auto r = session.Get();
            if (r.status_code == 200) {
                if (r.text.size() > max_html_page_size) r.text.resize(max_html_page_size);                
                if ((doc = gumbo_parse(r.text.c_str())) != nullptr) {
                    // Extract all links (internal and external) from the current page
                    if (!no_new_urls && !no_new_urls_auto) extract_links(doc->root, url);
                    // Extract all image links from the current page
                    std::string page_title = get_page_title(doc->root);
                    if (page_title.empty()) page_title = get_first_h1_text(doc->root);
                    extract_image_links(doc->root, url, page_title);
                }
            }
            else {
                lck.lock();
                memory_storage.update_all(set(c(&UrlData::status_code) = r.status_code), where(c(&UrlData::url) == url));
                lck.unlock();
            }
        }
        catch(...) {
            if (verbose) std::cout << "Critical issue occured during a web page analysis: " << url << std::endl;
            lck.lock();
            memory_storage.update_all(set(c(&UrlData::status_code) = 503), where(c(&UrlData::url) == url));
            lck.unlock();
        }
        if (doc != nullptr) gumbo_destroy_output(&kGumboDefaultOptions, doc);
        total_pages++;        
    }
}

// Copy image files from source to destination recursively
static size_t nb_imgs = 0;
const size_t nb_imgs_display = 5000;
void copy_and_delete_images(const boost::filesystem::path& src, const boost::filesystem::path& dst) {    
    if (!is_directory(src)) return;
    // Create destination folder if it does not exist
    if (!exists(dst)) create_directory(dst);
    // Iterate over files in source directory
    for (boost::filesystem::recursive_directory_iterator it(src), end; it != end && !stop_requested; ++it) {
        if (boost::filesystem::is_regular_file(*it) && it->path().extension() == ".jpg") {
            auto p = it->path();
            std::string md5_hash = calculate_md5_from_path(p);
            // Split the MD5 hash into two parts: the first 2 characters and the remaining characters
            std::string top_folder_name = md5_hash.substr(0, 1);
            std::string sub_folder_name = md5_hash.substr(1, 1);
            std::string file_name = md5_hash.substr(2);
            std::string folder_pathname = dst.string();
            folder_pathname += "/" + top_folder_name;
            if (!boost::filesystem::exists(folder_pathname)) boost::filesystem::create_directory(folder_pathname);
            folder_pathname += "/" + sub_folder_name;
            if (!boost::filesystem::exists(folder_pathname)) boost::filesystem::create_directory(folder_pathname);
            folder_pathname += "/" + file_name + ".jpg";
            copy_file(p, boost::filesystem::path(folder_pathname), boost::filesystem::copy_option::overwrite_if_exists);
            boost::filesystem::remove(p);
            if ((nb_imgs++ % nb_imgs_display) == 0) std::cout << ".";
        }
    }
}

// Move image files and metadata
void move(const boost::filesystem::path& src_root, const boost::filesystem::path& dst_root) {
    const boost::filesystem::path img_cache_dir = src_root / "img_cache";
    const boost::filesystem::path db_dest_path = dst_root / "queues.db";

    // Copy image files recursively
    std::cout << "Moving images ";
    copy_and_delete_images(img_cache_dir, dst_root / img_cache_dir.filename());
    std::cout << " done" << std::endl << std::endl;

    // Open the detination sqlite database and insert metadata. Iterate over all images in the database
    // For each image, read metadata and insert into destination database
    std::cout << "Updating metadata... ";
    auto dst_storage = make_storage(db_dest_path.string(),
        make_table("images",
            make_column("url", &ImageData::url, unique()),
            make_column("alt", &ImageData::alt),
            make_column("source", &ImageData::source_page),
            make_column("surrounding", &ImageData::surrounding_text),
            make_column("file_size", &ImageData::file_size),
            make_column("width", &ImageData::width),
            make_column("height", &ImageData::height),
            make_column("mime", &ImageData::mime),
            make_column("last_seen", &ImageData::last_seen))
    );
    dst_storage.sync_schema();
    dst_storage.transaction([&]() mutable {                
        auto images_data = storage.get_all<ImageData>();
        for (auto& img : images_data) {
            try { dst_storage.insert(img); }
            catch (...) {}
        }
        images_data.clear();
        return true;
        });
    std::cout << " done" << std::endl << std::endl;
}

// Function to synchronize image cache on disk and info into a database
void sync_image_cache(void) {
    // Create indexed STL structure for image files
    const boost::filesystem::path img_cache_dir = boost::filesystem::current_path() / "img_cache";
    std::cout << "Reading the image cache ";
    std::map<std::string, boost::filesystem::path> image_files;
    for (boost::filesystem::recursive_directory_iterator it(img_cache_dir), end; it != end && !stop_requested; ++it) {
        if (boost::filesystem::is_regular_file(*it) && it->path().extension() == ".jpg") {
            auto p = it->path();
            image_files[calculate_md5_from_path(p)] = p;
            if ((nb_imgs++ % nb_imgs_display) == 0) std::cout << ".";
        }
    }
    std::cout << std::endl << " done" << std::endl;

    // Create indexed STL structure for image urls in database
    std::cout << "Reading the database ";
    nb_imgs = 0;
    std::map<std::string, std::string> image_urls;
    auto img_urls = storage.select(&ImageData::url);
    for (auto &src_url : img_urls) {
        size_t md5_as_int = boost::hash<std::string>{}(src_url); // Calculate the MD5 hash of the string
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << md5_as_int;
        image_urls[ss.str()] = src_url;
        if ((nb_imgs++ % nb_imgs_display) == 0) std::cout << ".";
        if (stop_requested) break;
    }
    std::cout << std::endl << " done" << std::endl;
    img_urls.clear();

    // Remove files on disk for which the hash is not present in the database
    std::cout << "Updating the image cache ";
    nb_imgs = 0;
    size_t removed_imgs = 0;
    for (auto& it : image_files) {
        if (image_urls.find(it.first) == image_urls.end()) {
            boost::filesystem::remove(it.second);
            removed_imgs++;
        }
        if ((nb_imgs++ % nb_imgs_display) == 0) std::cout << ".";
        if (stop_requested) break;
    }
    std::cout << " done - " << removed_imgs << " image(s) removed)" << std::endl << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up signal handler for SIGINT (Ctrl+C)
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE)) {
        std::cout << "Signal cannot to manage..." << std::endl;
    }

    // Manage options
    po::options_description desc("Options");
    desc.add_options()
        ("help,h", "Show help message")
        ("verbose,v", "Set the verbose mode")
        ("auto-flush,f", "Activate the metadata autoflush")
        ("no-new-urls,u", "Don't add new urls to the queue")
        ("refresh-time,r", po::value<int>()->default_value(20), "Set the refresh stats time")
        ("threads,t", po::value<int>()->default_value(std::thread::hardware_concurrency()), "Set the total threads number")
        ("add-url,a", "Add a new starting URL")
        ("move-cache,m", po::value<std::string>(), "Move the image cache to another drive")
        ("sync-cache,s", "Synchronize the image cache with the database");
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << std::endl;
            return 0;
        }
        if (vm.count("move-cache")) {
            std::string sync_dst_root = vm["move-cache"].as<std::string>();
            move(boost::filesystem::current_path(), boost::filesystem::path(sync_dst_root));
            return 0;
        }
        if (vm.count("sync-cache")) {
            sync_image_cache();
            return 0;
        }
        
        // Initialize the storage
        storage.sync_schema();
        memory_storage.sync_schema();

        // Read parameters
        verbose = vm.count("verbose") ? true : false;
        bool auto_flush = vm.count("auto-flush") ? true : false;
        no_new_urls = vm.count("no-new-urls") ? true : false;
        int refresh_time = vm["refresh-time"].as<int>();
        size_t num_threads = std::min<std::size_t>(max_threads, vm["threads"].as<int>());        
        std::string start_url = vm.count("add-url") ? vm["add-url"].as<std::string>() : "https://www.starting_url.com/my_dir";
        boost::algorithm::trim(start_url);
        boost::replace_all(start_url, " ", "%20");        
        if (start_url.back() == '/') start_url.pop_back();
        
        // Copy all URL metadata from the disk-based database to the in-memory database
        std::cout << "Loading the metadata from disk... ";
        std::unique_lock<std::mutex> lck(mtx);
        auto urls_data = storage.get_all<UrlData>();
        for (auto& url : urls_data) memory_storage.insert(url);
        urls_data.clear();
        std::string last_crawled(""), last_seen = get_current_time();
        UrlData data{ start_url, std::make_unique<std::string>(last_crawled), last_seen };
        try { memory_storage.insert(data); }
        catch(...) {}
        // Copy all image metadata from the disk-based database to the in-memory database
        auto images_data = storage.get_all<ImageData>();
        for (auto& img : images_data) memory_storage.insert(img);
        images_data.clear();
        lck.unlock();
        std::cout << "done" << std::endl;
       
        std::cout << "Starting the spider with " << num_threads << " threads... ";
        std::thread spider_threads[max_threads];
        for (size_t i = 0; i < num_threads; ++i) spider_threads[i] = std::thread(spider);
        std::cout << "done" << std::endl;

        ElapsedTime stats_timer, flush_timer;
        if (!verbose) {
            std::cout << std::endl << "| Crawler pages | Crawled images | Pending pages | Visited pages | Visited images | Cached images |" << std::endl;
            std::cout << "|---------------|----------------|---------------|---------------|----------------|---------------|" << std::endl;
        }
        while (!stop_requested) {            
            if (stats_timer.getSeconds() < refresh_time) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                continue;
            }                        

            lck.lock();            
            size_t num_pending_web_pages = memory_storage.count<UrlData>(where(c(&UrlData::last_crawled) == ""));
            size_t num_visited_web_pages = memory_storage.count<UrlData>(where((c(&UrlData::last_crawled) != "") and c(&UrlData::status_code) == 200));
            size_t num_visited_images = memory_storage.count<ImageData>(where(c(&ImageData::mime) != unsupported_image_mime));
            size_t num_cached_images = memory_storage.count<ImageData>(where(c(&ImageData::file_size) > 0));
            if (flush_timer.getSeconds() >= auto_flush_time) {
                memory_storage.transaction([&]() mutable {
                    memory_storage.remove_all<UrlData>(where((c(&UrlData::last_crawled) != "") and (c(&UrlData::status_code) != 200)));
                    memory_storage.remove_all<ImageData>(where(c(&ImageData::mime) == unsupported_image_mime));
                    return true;
                    });
                if (auto_flush) {
                    storage.transaction([&]() mutable {
                        // Remove all entries in the tables
                        storage.remove_all<UrlData>();
                        storage.remove_all<ImageData>();
                        // Insert new metadata
                        urls_data = memory_storage.get_all<UrlData>();
                        for (auto& url : urls_data) storage.insert(url);
                        urls_data.clear();
                        images_data = memory_storage.get_all<ImageData>();
                        for (auto& img : images_data) storage.insert(img);
                        images_data.clear();
                        return true;
                        });
                }
                flush_timer.reset();
            }
            lck.unlock();
            
            // Display stats
            if (verbose) {
                std::cout << std::endl << "| Crawler pages | Crawled images | Pending pages | Visited pages | Visited images | Cached images |" << std::endl;
                std::cout << "|---------------|----------------|---------------|---------------|----------------|---------------|" << std::endl;
                std::cout << "| " << std::setw(13) << total_pages << " | " << std::setw(14) << total_images << " | " << std::setw(13) << num_pending_web_pages << " | " << std::setw(13) << num_visited_web_pages << " | " << std::setw(14) << num_visited_images << " | " << std::setw(13) << num_cached_images << " |" << std::endl;
            }
            else if(!stop_requested) {
                std::cout << "| " << std::setw(13) << total_pages << " | " << std::setw(14) << total_images << " | " << std::setw(13) << num_pending_web_pages << " | " << std::setw(13) << num_visited_web_pages << " | " << std::setw(14) << num_visited_images << " | " << std::setw(13) << num_cached_images << " |\r";
            }

            if (!no_new_urls_auto && num_pending_web_pages >= urls_queue_threshold_max) no_new_urls_auto = true;
            if (no_new_urls_auto && num_pending_web_pages < urls_queue_threshold_min) no_new_urls_auto = false;            
            stats_timer.reset();
        }
        for (int i = 0; i < num_threads; ++i) spider_threads[i].join();
        
        // Copy all URL metadata from the in-memory database to the disk-based database
        std::cout << std::endl << std::endl << "Saving the metadata on disk... ";
        storage.transaction([&]() mutable {
            // Optimize structures
            memory_storage.remove_all<UrlData>(where((c(&UrlData::last_crawled) != "") and (c(&UrlData::status_code) != 200)));
            memory_storage.remove_all<ImageData>(where(c(&ImageData::mime) == unsupported_image_mime));
            // Remove all entries in the tables
            storage.remove_all<UrlData>();
            storage.remove_all<ImageData>();
            // Insert new metadata
            urls_data = memory_storage.get_all<UrlData>();
            for (auto& url : urls_data) storage.insert(url);
            urls_data.clear();
            images_data = memory_storage.get_all<ImageData>();
            for (auto& img : images_data) storage.insert(img);
            images_data.clear();
            return true;
        });
        std::cout << "done" << std::endl;        

        return 0;
    }
    catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << std::endl << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }
}