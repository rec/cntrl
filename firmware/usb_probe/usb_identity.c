#include <usb_names.h>

struct usb_string_descriptor_struct usb_string_product_name = {
    38, 3,
    {'c', 'n', 't', 'r', 'l', ' ', 'e', 'x', 'p', 'e', 'r', 'i', 'm', 'e', 'n', 't', 'a', 'l'}
};

struct usb_string_descriptor_struct usb_string_manufacturer_name = {
    12, 3, {'c', 'n', 't', 'r', 'l'}
};
