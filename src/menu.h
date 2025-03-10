// Menu item structure
struct MenuItem {
  const char* name;
  unsigned int id;
  unsigned int parentID;
};

// Define menu items
#define MENU_MAIN 0
#define MENU_FEED_NOW 1
#define MENU_SETTINGS 2
#define MENU_SCHEDULE 3
#define MENU_WIFI 4
#define MENU_BACK 5
#define MENU_FEED_AMOUNT_10 6
#define MENU_FEED_AMOUNT_20 7
#define MENU_FEED_AMOUNT_30 8
#define MENU_SCHEDULE_1 9
#define MENU_SCHEDULE_2 10
#define MENU_SCHEDULE_3 11
#define MENU_WIFI_CONNECT 12
#define MENU_WIFI_RESET 13

#define MENU_ITEMS_COUNT (sizeof(menuItems) / sizeof(MenuItem))

// Menu item definitions
const MenuItem menuItems[] = {
    {"Main Menu", MENU_MAIN, MENU_MAIN},
    {"Feed Now", MENU_FEED_NOW, MENU_MAIN},
    {"Settings", MENU_SETTINGS, MENU_MAIN},
    {"Schedule", MENU_SCHEDULE, MENU_MAIN},
    {"WiFi Setup", MENU_WIFI, MENU_MAIN},
    {"Back", MENU_BACK, MENU_MAIN},
    {"10g of Food", MENU_FEED_AMOUNT_10, MENU_FEED_NOW},
    {"20g of Food", MENU_FEED_AMOUNT_20, MENU_FEED_NOW},
    {"30g of Food", MENU_FEED_AMOUNT_30, MENU_FEED_NOW},
    {"At 8:00 AM", MENU_SCHEDULE_1, MENU_SCHEDULE},
    {"At 4:00 PM", MENU_SCHEDULE_2, MENU_SCHEDULE},
    {"At 10:00 PM", MENU_SCHEDULE_3, MENU_SCHEDULE},
    {"Connect WiFi", MENU_WIFI_CONNECT, MENU_WIFI},
    {"Reset WiFi", MENU_WIFI_RESET, MENU_WIFI}};
