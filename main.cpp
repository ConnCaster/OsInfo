#include <sys/utsname.h>
#include <fstream>
#include <iostream>
#include <string>


static std::string ParseValue(const std::string& line) {
    size_t pos = line.find('=');
    if (pos == std::string::npos) return "";
    std::string value = line.substr(pos + 1);
    if (!value.empty() && value.front() == '"') value.erase(0, 1);
    if (!value.empty() && value.back() == '"') value.pop_back();
    return value;
}


class DistroInfo {
public:
    virtual ~DistroInfo() = 0;
    virtual int GetDistroInfo() = 0;


};

class DistroInfoImpl : public DistroInfo {
public:
    std::string GetName() const {
        return distro_name_;
    }

    std::string GetVersion() const {
        return distro_version_;
    }
protected:
    std::string distro_name_{};
    std::string distro_version_{};
};

class OsReleaseDistroInfo : public DistroInfoImpl {
public:
    ~OsReleaseDistroInfo() override = default;

    int GetDistroInfo() override {
        std::ifstream file(kOsReleasePath_);
        if (!file.is_open()) {
            return 1;
        }
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("PRETTY_NAME=") == 0 || line.find("NAME=") == 0) {
                distro_name_ = ParseValue(line);
            }
            if (line.find("VERSION_ID=") == 0) {
                distro_version_ = ParseValue(line);
            }
            if (!distro_name_.empty() && !distro_version_.empty()) {
                break;
            }
        }
        return 0;
    }

private:
    const std::string kOsReleasePath_ = "/etc/os-release";
};

class OSInfo {
public:
    OSInfo() = default;
    ~OSInfo() = default;

    int GetOsInfo() {
        // uname()
        utsname info{};
        if (uname(&info) == 0) {
            architecture_ = info.machine;
        }



        return 0;
    }

    void Print() const {
        std::cout << "=== Информация об ОС ===" << std::endl;
        std::cout << "Дистрибутив: " << distro_name_ << std::endl;
        std::cout << "Версия: " << distro_version_ << std::endl;
        std::cout << "Архитектура: " << architecture_ << std::endl;
    }

private:


private:
    std::string distro_name_{};
    std::string distro_version_{};
    std::string architecture_{};

    const std::string kDebianVersion = "/etc/debian_version";
};

int main() {
    OSInfo os;
    os.GetOsInfo();
    os.Print();
    return 0;
}