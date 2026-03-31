#include <iostream>
#include <iterator>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <array>
#include <unistd.h> // Для access()

class IPackageList {
public:
    virtual ~IPackageList() = default;
    virtual int ParsePackageList() = 0;
    virtual std::vector<std::string> GetPackageList() = 0;
};

class RpmPackageList : public IPackageList {
public:
    ~RpmPackageList() override = default;

    int ParsePackageList() override {
        return ExecuteCommand("rpm -qa --qf '%{NAME}\\n'", rpm_packages_);
    }

    std::vector<std::string> GetPackageList() override {
        return rpm_packages_;
    }

private:
    std::vector<std::string> rpm_packages_{};

    int ExecuteCommand(const std::string& cmd, std::vector<std::string>& output) {
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return -1;

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            // Удаляем символ новой строки
            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }
            if (!line.empty()) {
                output.push_back(line);
            }
        }
        return pclose(pipe);
    }
};

class DebPackageList : public IPackageList {
public:
    ~DebPackageList() override = default;

    int ParsePackageList() override {
        return ExecuteCommand("dpkg-query -W -f='${Package}\\n'", deb_packages_);
    }

    std::vector<std::string> GetPackageList() override {
        return deb_packages_;
    }

private:
    std::vector<std::string> deb_packages_{};

    int ExecuteCommand(const std::string& cmd, std::vector<std::string>& output) {
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return -1;

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            if (!line.empty() && line.back() == '\n') {
                line.pop_back();
            }
            if (!line.empty()) {
                output.push_back(line);
            }
        }
        return pclose(pipe);
    }
};

class PackageListHandler {
public:
    PackageListHandler() {
        if (IsRpmSystem()) {
            packages_handler_ = std::make_unique<RpmPackageList>();
            system_type_ = "RPM";
        } else if (IsDebSystem()) {
            packages_handler_ = std::make_unique<DebPackageList>();
            system_type_ = "DEB";
        } else {
            system_type_ = "UNKNOWN";
        }
    }

    ~PackageListHandler() = default;

    int ParsePackageList() {
        if (!packages_handler_) {
            std::cerr << "Unsupported system type" << std::endl;
            return -1;
        }
        return packages_handler_->ParsePackageList();
    }

    std::vector<std::string> GetPackageList() {
        if (!packages_handler_) return {};
        return packages_handler_->GetPackageList();
    }

    void PrintSystemInfo() const {
        std::cout << "Detected system type: " << system_type_ << std::endl;
    }

private:
    std::unique_ptr<IPackageList> packages_handler_;
    std::string system_type_;

    // Проверка наличия исполняемого файла
    bool IsRpmSystem() {
        return access("/usr/bin/rpm", F_OK) == 0;
    }

    bool IsDebSystem() {
        return access("/usr/bin/dpkg-query", F_OK) == 0;
    }
};

int main() {
    PackageListHandler handler;
    handler.PrintSystemInfo();

    if (handler.ParsePackageList() == 0) {
        // Исправлено: std::string вместо std::ostream
        std::copy(handler.GetPackageList().begin(),
                  handler.GetPackageList().end(),
                  std::ostream_iterator<std::string>(std::cout, "\n"));
    }

    return 0;
}