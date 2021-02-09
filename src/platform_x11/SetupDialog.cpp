#include <iostream>
#include "../Renderer.h"
#ifdef BONZOMATIC_ENABLE_GTK
#include <string>
#include <gtk/gtk.h>
#include <locale.h>
#include "dialog.h"

class GTKSetupDialog {
  RENDERER_SETTINGS * setup;
  NETWORK_SETTINGS * network;

  GtkBuilder* builder;
  GtkWidget* dialog;

  GtkWidget* GetWidget(const char* id) {
    return GTK_WIDGET(gtk_builder_get_object (builder, id));
  }

  void SetWidgetText(const char* id, const std::string& text) {
    gtk_entry_set_text (GTK_ENTRY(GetWidget(id)), text.c_str());
  }

  std::string GetWidgetText(const char* id) {
    return gtk_entry_get_text (GTK_ENTRY(GetWidget(id)));
  }

  void SetWidgetToggled(const char* id, bool toggled) {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(GetWidget(id)), toggled);
  }

  bool GetWidgetToggled(const char* id) {
    return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(GetWidget(id)));
  }

  void SetWidgetActiveId(const char* id, const std::string& active_id) {
    gtk_combo_box_set_active_id (GTK_COMBO_BOX(GetWidget(id)), active_id.c_str());
  }

  std::string GetWidgetActiveId(const char* id) {
    const char* active_id = gtk_combo_box_get_active_id (GTK_COMBO_BOX(GetWidget(id)));
    if (active_id == NULL) return "";
    return active_id;
  }

public:

  GTKSetupDialog(RENDERER_SETTINGS * settings, NETWORK_SETTINGS* netSettings) {
    setup = settings;
    network = netSettings;
    gtk_init (NULL, NULL);
    builder = gtk_builder_new_from_string (dialog_glade, dialog_glade_len);
    dialog = GetWidget("dialog");
    gtk_window_set_default_size (GTK_WINDOW(dialog), 500, 100);

    SetWidgetText("width", std::to_string(setup->nWidth));
    SetWidgetText("height", std::to_string(setup->nHeight));
    SetWidgetToggled("fullscreen", setup->windowMode == RENDERER_WINDOWMODE_FULLSCREEN);
    SetWidgetToggled("borderless", setup->windowMode == RENDERER_WINDOWMODE_BORDERLESS);
    SetWidgetToggled("vsync", setup->bVsync);

    std::string ServerName;
    std::string RoomName;
    std::string NickName;
    Network_Break_URL(network->ServerURL, ServerName, RoomName, NickName);
    SetWidgetText("server", ServerName);
    SetWidgetText("room", RoomName);
    SetWidgetText("nick", NickName);
    SetWidgetActiveId("mode", network->NetworkModeString);
    SetWidgetToggled("network_enable", network->EnableNetwork);
  }

  bool Open() {
    bool success = gtk_dialog_run (GTK_DIALOG(dialog)) == 1;
    if (success) {
      setup->nWidth = std::stoi(GetWidgetText("width"));
      setup->nHeight = std::stoi(GetWidgetText("height"));
      if (GetWidgetToggled("fullscreen")) {
        setup->windowMode = RENDERER_WINDOWMODE_FULLSCREEN;
      }
      if (GetWidgetToggled("borderless")) {
        setup->windowMode = RENDERER_WINDOWMODE_BORDERLESS;
      }
      setup->bVsync = GetWidgetToggled("vsync");

      std::string ServerName = GetWidgetText("server");
      std::string RoomName = GetWidgetText("room");
      std::string NickName = GetWidgetText("nick");
      network->ServerURL = ServerName + "/" + RoomName + "/" + NickName;
      network->NetworkModeString = GetWidgetActiveId("mode");
      network->EnableNetwork = GetWidgetToggled("network_enable");
    }
    gtk_widget_destroy (dialog);
    while (gtk_events_pending ()) gtk_main_iteration (); // clear gtk event queue
    setlocale(LC_ALL, "C");
    return success;
  }
};

bool Renderer::OpenSetupDialog( RENDERER_SETTINGS * settings, NETWORK_SETTINGS* netSettings )
{
  GTKSetupDialog dlg(settings, netSettings);
  return dlg.Open();
}
#else
bool Renderer::OpenSetupDialog( RENDERER_SETTINGS * settings, NETWORK_SETTINGS* netSettings )
{
  std::cerr << __FUNCTION__ << " STUB" << std::endl;
  return true;
}
#endif