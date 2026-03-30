#include <sys/utsname.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>

// ============================================================================
// Helper: парсинг значений из файлов типа /etc/os-release и /etc/lsb-release
// ============================================================================
static std::string ParseValue(const std::string &line) {
    size_t pos = line.find('=');
    if (pos == std::string::npos) return "";

    std::string value = line.substr(pos + 1);

    // Убираем кавычки, если есть (для os-release)
    if (!value.empty() && value.front() == '"') value.erase(0, 1);
    if (!value.empty() && value.back() == '"') value.pop_back();

    // Trim пробелов
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    value.erase(value.find_last_not_of(" \t\r\n") + 1);

    return value;
}

// ============================================================================
// Reader: базовый интерфейс для чтения разных файлов
// ============================================================================
class DistroFileReader {
public:
    virtual ~DistroFileReader() = default;
    virtual int Read(std::string &name, std::string &version) = 0;
};

// ============================================================================
// Реализация для /etc/os-release (стандарт freedesktop.org)
// ============================================================================
class OsReleaseReader : public DistroFileReader {
public:
    int Read(std::string &name, std::string &version) override {
        std::ifstream file("/etc/os-release");
        if (!file.is_open()) return 1;

        std::string line;
        while (std::getline(file, line)) {
            if (line.find("PRETTY_NAME=") == 0 || line.find("NAME=") == 0) {
                name = ParseValue(line);
            } else if (line.find("VERSION_ID=") == 0) {
                version = ParseValue(line);
            }
            if (!name.empty() && !version.empty()) break;
        }
        return 0;
    }
};

// ============================================================================
// Реализация для /etc/lsb-release (Debian/Ubuntu style)
// DISTRIB_ID="ManjaroLinux"
// DISTRIB_RELEASE="26.0.3"
// DISTRIB_CODENAME="Anh-Linh"
// DISTRIB_DESCRIPTION="Manjaro Linux"
// ============================================================================
class LsbReleaseReader : public DistroFileReader {
public:
    int Read(std::string &name, std::string &version) override {
        std::ifstream file("/etc/lsb-release");
        if (!file.is_open()) return 1;

        std::string line;
        while (std::getline(file, line)) {
            if (line.find("DISTRIB_DESCRIPTION=") == 0) {
                name = ParseValue(line);
            } else if (line.find("DISTRIB_RELEASE=") == 0) {
                version = ParseValue(line);
            }
        }

        return 0;
    }
};

// ============================================================================
// Реализация для /etc/redhat-release (Red Hat / Red OS style)
// ============================================================================
class RedHatReleaseReader : public DistroFileReader {
public:
    int Read(std::string &name, std::string &version) override {
        std::ifstream file("/etc/redhat-release");
        if (!file.is_open()) return 1;

        std::string line;
        if (!std::getline(file, line) || line.empty()) return false;

        // Пример: "Red OS release 7.3.6 (Mosquito)"
        // Ищем "release" и извлекаем версию после него
        const std::string release_marker = " release ";
        size_t pos = line.find(release_marker);
        if (pos != std::string::npos) {
            name = line.substr(0, pos); // Всё до " release "
            size_t ver_start = pos + release_marker.length();
            size_t ver_end = line.find(' ', ver_start);
            if (ver_end == std::string::npos) ver_end = line.find('(', ver_start);
            if (ver_end == std::string::npos) ver_end = line.length();
            version = line.substr(ver_start, ver_end - ver_start);
        } else {
            // Если формат нестандартный — возвращаем всю строку как имя
            name = line;
        }

        return 0;
    }
};

// ============================================================================
// Fallback для /etc/debian_version (только версия, имя — "Debian")
// ============================================================================
class DebianVersionReader : public DistroFileReader {
public:
    int Read(std::string &name, std::string &version) override {
        std::ifstream file("/etc/debian_version");
        if (!file.is_open()) return 1;

        if (std::getline(file, version) && !version.empty()) {
            name = "Debian GNU/Linux";
            // Trim
            version.erase(0, version.find_first_not_of(" \t\r\n"));
            version.erase(version.find_last_not_of(" \t\r\n") + 1);
            return true;
        }
        return 0;
    }
};

// ============================================================================
// Main class: агрегирует информацию об ОС
// ============================================================================
class OSInfo {
public:
    OSInfo() = default;

    int GetOsInfo() {
        utsname sys_info{};
        if (uname(&sys_info) != 0) {
            std::cerr << "Warning: uname() failed" << std::endl;
            return 1;
        }
        architecture_ = sys_info.machine;

        // 2. Цепочка fallback для информации о дистрибутиве
        std::vector<std::unique_ptr<DistroFileReader> > readers;
        readers.emplace_back(std::make_unique<OsReleaseReader>()); // приоритет 1
        readers.emplace_back(std::make_unique<LsbReleaseReader>()); // приоритет 2
        readers.emplace_back(std::make_unique<RedHatReleaseReader>()); // приоритет 3
        readers.emplace_back(std::make_unique<DebianVersionReader>()); // приоритет 4

        for (const auto &reader: readers) {
            if (reader->Read(distro_name_, distro_version_)) {
                if (!distro_name_.empty() && !distro_version_.empty()) {
                    break;
                }
            }
        }
        return 0;
    }

    void Print() const {
        std::cout << "=== Информация об ОС ===" << std::endl;
        std::cout << "Дистрибутив: " << (distro_name_.empty() ? "Unknown" : distro_name_) << std::endl;
        std::cout << "Версия дистрибутива: " << (distro_version_.empty() ? "Unknown" : distro_version_) << std::endl;
        std::cout << "Архитектура: " << (architecture_.empty() ? "Unknown" : architecture_) << std::endl;
    }

    const std::string &GetDistroName() const { return distro_name_; }
    const std::string &GetDistroVersion() const { return distro_version_; }
    const std::string &GetArchitecture() const { return architecture_; }

private:
    // Данные из uname()
    std::string architecture_;

    // Данные о дистрибутиве
    std::string distro_name_;
    std::string distro_version_;
};

// ============================================================================
// Entry point
// ============================================================================
int main() {
    OSInfo os;
    if (os.GetOsInfo() != 0) {
        std::cerr << "Error: Failed to collect OS information" << std::endl;
        return 1;
    }
    os.Print();
    return 0;
}
