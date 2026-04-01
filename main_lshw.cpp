#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <filesystem>
#include "json.hpp"

using json = nlohmann::json;

// ============================================================================
// Упрощённая структура: только строки и векторы строк
// ============================================================================
struct HardwareSummary {
    std::string cpu; // "Model X cores"
    std::string motherboard; // "Vendor Product"
    std::vector<std::string> memory; // ["Vendor Serial Product Size", ...]
    std::vector<std::string> network; // ["name: Product, Vendor, Speed, Desc", ...]
    std::vector<std::string> display; // ["name: Product, Vendor, Res, Driver", ...]
    std::vector<std::string> storage; // ["name: Product, Vendor, Size, Type, FS", ...]
    std::vector<std::string> usb;
};

// ============================================================================
// Сериализация в нужный формат
// ============================================================================
void to_json(json &j, const HardwareSummary &s) {
    j = json{
        {"cpu", s.cpu},
        {"motherboard", s.motherboard},
        {"memory", s.memory},
        {"network", s.network},
        {"display", s.display},
        {"storage", s.storage},
        {"usb", s.usb}
    };
}

// ============================================================================
// Парсер lshw (JSON) - упрощённая версия
// ============================================================================
class LshwHardwareCollector {
public:
    int Collect() {
        std::string json_data = ExecuteCommand("lshw -json 2>/dev/null");
        if (json_data.empty()) return -1;

        try {
            json data = json::parse(json_data);

            // Материнская плата
            std::string mb_vendor = data.value("vendor", "");
            std::string mb_product = data.value("product", "");
            summary_.motherboard = mb_vendor + " " + mb_product;

            if (data.contains("children") && data["children"].is_array()) {
                for (const auto &child: data["children"]) {
                    ParseNode(child);
                }
            }
        } catch (const json::parse_error &e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            return -1;
        }
        return 0;
    }

    void PrintJson() const {
        std::cout << json(summary_).dump(4) << std::endl;
    }

    json GetJson() const { return summary_; }

private:
    HardwareSummary summary_{};

    void ParseNode(const json &node) {
        if (!node.is_object()) return;

        std::string cls = node.value("class", "");

        // === CPU ===
        if (cls == "processor" && summary_.cpu.empty()) {
            std::string model = node.value("product", "");
            int cores = GetNumericValue<int>(node, "configuration/cores", 1);
            summary_.cpu = model + ", " + std::to_string(cores) + " cores";
        }
        // === RAM (только занятые слоты) ===
        else if (cls == "memory" && node.value("id", "") == "memory") {
            if (node.contains("children") && node["children"].is_array()) {
                for (const auto &bank: node["children"]) {
                    if (bank.value("class", "") == "memory") {
                        std::string product = bank.value("product", "Unknown");
                        std::string serial = bank.value("serial", "Unknown");
                        std::string vendor = bank.value("vendor", "Unknown");
                        unsigned long long size = GetNumericValue<unsigned long long>(bank, "size", 0);
                        std::string desc = bank.value("description", "");

                        // Пропускаем пустые слоты
                        if (size > 0 && product != "Unknown" && desc.find("[empty]") == std::string::npos) {
                            std::string size_fmt = FormatSize(size);
                            // Формат: "Vendor Serial Product Size"
                            summary_.memory.push_back(vendor + ", serial: " + serial + ", product: " + product + ", " + size_fmt);
                        }
                    }
                }
            }
        }
        // === Network ===
        else if (cls == "network") {
            std::string name = GetStringField(node, "logicalname");
            if (!name.empty() && name != "UNKNOWN" && name.find("lo") == std::string::npos) {
                std::string product = node.value("product", "");
                std::string vendor = node.value("vendor", "");
                std::string desc = node.value("description", "");
                std::string speed = "";

                if (node.contains("configuration") && node["configuration"].is_object()) {
                    speed = node["configuration"].value("speed", "");
                }

                // Формат: "name: Product, Vendor, Speed, Description"
                summary_.network.push_back(product + ", " + vendor + ", " + speed + ", " + desc);
            }
        }
        // === Display ===
        else if (cls == "display") {
            std::string name = GetStringField(node, "logicalname");
            std::string product = node.value("product", "");
            std::string vendor = node.value("vendor", "");
            std::string driver = "";
            std::string resolution = "";

            if (node.contains("configuration") && node["configuration"].is_object()) {
                const auto &cfg = node["configuration"];
                driver = cfg.value("driver", "");
                resolution = cfg.value("resolution", "");
            }

            // Формат: "name: Product, Vendor, Resolution, Driver"
            if (!name.empty()) {
                summary_.display.push_back(product + ", " + vendor + ", " + resolution + ", " + driver);
            }
        }
        // === Storage: NVMe устройства (данные в родителе, размер в детях) ===
        if (cls == "storage" && node.value("description", "").find("NVMe device") != std::string::npos) {
            // Берём серийник/вендор/продукт из родительского узла "NVMe device"
            std::string serial = node.value("serial", "Unknown");
            std::string product = node.value("product", "Unknown");
            std::string vendor = node.value("vendor", "Unknown");

            // Ищем дочерние диски (namespace) для получения размера и имени
            if (node.contains("children") && node["children"].is_array()) {
                for (const auto &child: node["children"]) {
                    if (child.value("class", "") == "disk" &&
                        child.value("description", "") == "NVMe disk") {
                        std::string logicalname = GetStringField(child, "logicalname");

                        unsigned long long size = GetNumericValue<unsigned long long>(child, "size", 0);
                        if (size == 0) continue;  // Пропускаем технические namespace без размера

                        if (!logicalname.empty() && logicalname.find("hwmon") == std::string::npos) {
                            unsigned long long size = GetNumericValue<unsigned long long>(child, "size", 0);
                            std::string size_fmt = FormatSize(size);
                            std::string filesystem = "";

                            // Ищем ФС в разделах диска
                            if (child.contains("children") && child["children"].is_array()) {
                                for (const auto &vol: child["children"]) {
                                    if (vol.value("class", "") == "volume" &&
                                        vol.contains("configuration") && vol["configuration"].is_object()) {
                                        filesystem = vol["configuration"].value("filesystem", "");
                                        if (!filesystem.empty()) break;
                                    }
                                }
                            }

                            summary_.storage.push_back(
                                logicalname + ": " + product + ", " + vendor + ", serial: " + serial + ", " +
                                size_fmt + ", NVMe disk, " + filesystem);
                        }
                    }
                }
            }
        }
        // === Storage: ATA/SATA диски (все данные в одном узле class="disk") ===
        else if (cls == "disk" && node.value("description", "") == "ATA Disk") {
            std::string logicalname = GetStringField(node, "logicalname");
            if (!logicalname.empty() && logicalname.find("hwmon") == std::string::npos) {
                std::string product = node.value("product", "Unknown");
                std::string vendor = node.value("vendor", "Unknown");
                std::string serial = node.value("serial", "Unknown");
                unsigned long long size = GetNumericValue<unsigned long long>(node, "size", 0);
                std::string size_fmt = FormatSize(size);
                std::string filesystem = "";

                // Поиск ФС в разделах
                if (node.contains("children") && node["children"].is_array()) {
                    for (const auto &child: node["children"]) {
                        if (child.value("class", "") == "volume" &&
                            child.contains("configuration") && child["configuration"].is_object()) {
                            filesystem = child["configuration"].value("filesystem", "");
                            if (!filesystem.empty()) break;
                        }
                    }
                }

                summary_.storage.push_back(
                    logicalname + ": " + product + ", " + vendor + ", " + serial + ", " +
                    size_fmt + ", ATA Disk, " + filesystem);
            }
        }
        // === Storage: SCSI Disk (аналогично ATA, но ищем описание "SCSI Disk") ===
        else if (cls == "disk" && node.value("description", "").find("SCSI Disk") != std::string::npos) {
            std::string logicalname = GetStringField(node, "logicalname");
            unsigned long long size = GetNumericValue<unsigned long long>(node, "size", 0);

            if (!logicalname.empty() && logicalname.find("hwmon") == std::string::npos && size > 0) {
                std::string product = node.value("product", "Unknown");
                std::string vendor = node.value("vendor", "Unknown");
                std::string serial = node.value("serial", "Unknown");
                std::string size_fmt = FormatSize(size);
                std::string filesystem = "";

                // Поиск ФС в разделах
                if (node.contains("children") && node["children"].is_array()) {
                    for (const auto &child: node["children"]) {
                        if (child.value("class", "") == "volume" &&
                            child.contains("configuration") && child["configuration"].is_object()) {
                            filesystem = child["configuration"].value("filesystem", "");
                            if (!filesystem.empty()) break;
                            }
                    }
                }

                summary_.storage.push_back(
                    logicalname + ": " + product + ", " + vendor + ", " + serial + ", " +
                    size_fmt + ", SCSI Disk, " + filesystem);
            }
        }
        // === USB Generic Devices (токены, смарт-карты и т.д.) ===
        else if (cls == "generic" && node.value("id", "").find("usb") != std::string::npos) {
            std::string product = node.value("product", "");
            std::string vendor = node.value("vendor", "");
            std::string desc = node.value("description", "");

            if (!product.empty() && !vendor.empty()) {
                std::string speed = "";
                if (node.contains("configuration") && node["configuration"].is_object()) {
                    speed = node["configuration"].value("speed", "");
                }

                // Формат: "Product, Vendor, Version, Speed, Description"
                summary_.usb.push_back(product + ", " + vendor + ", USB 2.0, " + speed + ", " + desc);
            }
        }

        // === Рекурсивный обход ===
        if (node.contains("children") && node["children"].is_array()) {
            for (const auto &child: node["children"]) {
                ParseNode(child);
            }
        }
    }

    // Вспомогательная функция для получения вложенного числового значения
    template<typename T>
    T GetNumericValue(const json &j, const std::string &path, T default_val = T{}) {
        size_t pos = path.find('/');
        if (pos != std::string::npos) {
            std::string key = path.substr(0, pos);
            std::string rest = path.substr(pos + 1);
            if (j.contains(key) && j[key].is_object()) {
                return GetNumericValue<T>(j[key], rest, default_val);
            }
            return default_val;
        }
        if (!j.contains(path)) return default_val;
        const auto &val = j[path];
        if (val.is_number()) return val.get<T>();
        if (val.is_string()) {
            try {
                if constexpr (std::is_same<T, unsigned long long>::value)
                    return static_cast<T>(std::stoull(val.get<std::string>()));
                else
                    return static_cast<T>(std::stoll(val.get<std::string>()));
            } catch (...) { return default_val; }
        }
        return default_val;
    }

    std::string GetStringField(const json &node, const std::string &key) {
        if (!node.contains(key)) return "";
        const auto &val = node[key];
        if (val.is_string()) return val.get<std::string>();
        if (val.is_array() && !val.empty()) {
            for (const auto &item: val) {
                if (item.is_string()) {
                    std::string s = item.get<std::string>();
                    if (s.find("/dev/snd/") == std::string::npos || s.find("card") != std::string::npos) {
                        return s;
                    }
                }
            }
        }
        return "";
    }

    std::string ExecuteCommand(const std::string &cmd, bool trim_newline = false) {
        std::string result;
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) result += buffer;
        pclose(pipe);
        if (trim_newline && !result.empty() && result.back() == '\n') result.pop_back();
        return result;
    }

    std::string FormatSize(unsigned long long bytes) {
        if (bytes == 0) return "N/A";
        const char *units[] = {"B", "KB", "MB", "GB", "TB"};
        int idx = 0;
        double size = static_cast<double>(bytes);
        while (size >= 1024 && idx < 4) {
            size /= 1024;
            idx++;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f %s", size, units[idx]);
        return std::string(buf);
    }
};

// ============================================================================
// Main
// ============================================================================
int main() {
    LshwHardwareCollector collector;
    if (collector.Collect() == 0) {
        collector.PrintJson();
    } else {
        std::cerr << "Failed to collect hardware info" << std::endl;
        return 1;
    }
    return 0;
}
