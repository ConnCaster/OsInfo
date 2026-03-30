#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <map>

// ============================================================================
// Структура: данные одного сетевого интерфейса
// ============================================================================
struct NetworkInterface {
    std::string name;
    std::string ipv4;
    std::string mac;
};

// ============================================================================
// Класс: получение системной информации (hostname)
// ============================================================================
class HostnameInfo {
public:
    static std::string GetHostname() {
        char hostname[256] = {0};
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            return hostname;
        }
        return "unknown";
    }
};

// ============================================================================
// Класс: получение сетевой информации (IP, MAC)
// ============================================================================
class NetworkInfo {
public:
    NetworkInfo() = default;

    // Собрать данные обо всех интерфейсах
    // Возврат: 0 — успех, 1 — ошибка
    int GetNetInfo() {
        interfaces_.clear();
        std::map<std::string, NetworkInterface> iface_map;

        ifaddrs* ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == -1) {
            return 1;
        }

        // Сокет для ioctl (MAC-адрес)
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

        for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_name) continue;

            const std::string name = ifa->ifa_name;

            // Создаём запись, если интерфейс ещё не встречался
            if (iface_map.find(name) == iface_map.end()) {
                iface_map[name] = {name, "", ""};
            }
            auto& iface = iface_map[name];

            // MAC-адрес через ioctl (только один раз на интерфейс)
            if (sock >= 0 && iface.mac.empty()) {
                ifreq ifr{};
                std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    unsigned char* hw = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
                    char mac_buf[18];
                    std::snprintf(mac_buf, sizeof(mac_buf),
                                  "%02X:%02X:%02X:%02X:%02X:%02X",
                                  hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);
                    iface.mac = mac_buf;
                }
            }

            // IPv4-адрес (обрабатываем только AF_INET)
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                char buf[INET_ADDRSTRLEN];
                auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
                if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
                    iface.ipv4 = buf;
                }
            }
        }

        if (sock >= 0) close(sock);
        freeifaddrs(ifaddr);

        // Конвертируем map в vector, оставляем только интерфейсы с данными
        for (auto& [name, iface] : iface_map) {
            if (!iface.ipv4.empty() || !iface.mac.empty()) {
                interfaces_.push_back(iface);
            }
        }

        return interfaces_.empty() ? 1 : 0;
    }

    const std::vector<NetworkInterface>& GetInterfaces() const {
        return interfaces_;
    }

    const NetworkInterface* FindByName(const std::string& name) const {
        for (const auto& iface : interfaces_) {
            if (iface.name == name) return &iface;
        }
        return nullptr;
    }

    const NetworkInterface* GetPrimary() const {
        for (const auto& iface : interfaces_) {
            if (iface.name != "lo") return &iface;
        }
        return interfaces_.empty() ? nullptr : &interfaces_[0];
    }

    void Print() const {
        std::cout << "Hostname: " << HostnameInfo::GetHostname() << std::endl;
        for (const auto& iface : interfaces_) {
            std::cout << "Interface: " << iface.name << std::endl;
            if (!iface.ipv4.empty()) std::cout << "  IP:  " << iface.ipv4 << std::endl;
            if (!iface.mac.empty())  std::cout << "  MAC: " << iface.mac << std::endl;
        }
    }

private:
    std::vector<NetworkInterface> interfaces_;
};

// ============================================================================
// Entry point
// ============================================================================
int main() {
    NetworkInfo net;
    if (net.GetNetInfo() != 0) {
        std::cerr << "Failed to collect network info" << std::endl;
        return 1;
    }
    net.Print();
    return 0;
}