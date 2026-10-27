#include "nic.h"
#include "e1000.h"
#include "rtl8139.h"
#include "virtio_net.h"
#include "vga.h"

enum { NIC_NONE = 0, NIC_E1000, NIC_RTL8139, NIC_VIRTIO };
static int nic_type = NIC_NONE;

bool nic_init(void) {
    nic_type = NIC_NONE;
    vga_print("  Probing NIC...\n");

    if (e1000_init()) {
        nic_type = NIC_E1000;
        vga_print("  using Intel e1000\n");
        return true;
    }
    if (rtl8139_init()) {
        nic_type = NIC_RTL8139;
        vga_print("  using Realtek RTL8139\n");
        return true;
    }
    if (virtio_net_init()) {
        nic_type = NIC_VIRTIO;
        vga_print("  using VirtIO-net\n");
        return true;
    }
    vga_print("  no supported NIC found\n");
    return false;
}

bool nic_send(const void* data, size_t len) {
    if (nic_type == NIC_E1000) return e1000_send(data, len);
    if (nic_type == NIC_RTL8139) return rtl8139_send(data, len);
    if (nic_type == NIC_VIRTIO) return virtio_net_send(data, len);
    return false;
}

size_t nic_recv(void* buf, size_t max_len) {
    if (nic_type == NIC_E1000) return e1000_recv(buf, max_len);
    if (nic_type == NIC_RTL8139) return rtl8139_recv(buf, max_len);
    if (nic_type == NIC_VIRTIO) return virtio_net_recv(buf, max_len);
    return 0;
}

void nic_get_mac(uint8_t mac[6]) {
    if (nic_type == NIC_E1000) e1000_get_mac(mac);
    else if (nic_type == NIC_RTL8139) rtl8139_get_mac(mac);
    else if (nic_type == NIC_VIRTIO) virtio_net_get_mac(mac);
    else for (int i = 0; i < 6; i++) mac[i] = 0;
}

bool nic_link_up(void) {
    if (nic_type == NIC_E1000) return e1000_link_up();
    if (nic_type == NIC_RTL8139) return rtl8139_link_up();
    if (nic_type == NIC_VIRTIO) return virtio_net_link_up();
    return false;
}

const char* nic_name(void) {
    if (nic_type == NIC_E1000) return "e1000";
    if (nic_type == NIC_RTL8139) return "rtl8139";
    if (nic_type == NIC_VIRTIO) return "virtio-net";
    return "none";
}
