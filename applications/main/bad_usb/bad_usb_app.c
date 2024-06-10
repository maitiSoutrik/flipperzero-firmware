#include "bad_usb_app_i.h"
#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <lib/toolbox/path.h>
#include <flipper_format/flipper_format.h>

#define BAD_USB_SETTINGS_PATH BAD_USB_APP_BASE_FOLDER "/.badusb.settings"
#define BAD_USB_SETTINGS_FILE_TYPE "Flipper BadUSB Settings File"
#define BAD_USB_SETTINGS_VERSION 1
#define BAD_USB_SETTINGS_DEFAULT_LAYOUT BAD_USB_APP_PATH_LAYOUT_FOLDER "/en-US.kl"

/**
 * @brief Custom event callback for the Bad USB application.
 *
 * This function is called when a custom event is triggered in the Bad USB application.
 * It asserts that the context is valid, casts the context to a BadUsbApp instance,
 * and then handles the custom event using the scene manager.
 *
 * @param context Pointer to the context, expected to be a BadUsbApp instance.
 * @param event The custom event identifier.
 * @return true if the event was handled successfully, false otherwise.
 */
static bool bad_usb_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    BadUsbApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

/**
 * @brief Back event callback for the Bad USB application.
 *
 * This function is called when a back event is triggered in the Bad USB application.
 * It asserts that the context is valid, casts the context to a BadUsbApp instance,
 * and then handles the back event using the scene manager.
 *
 * @param context Pointer to the context, expected to be a BadUsbApp instance.
 * @return true if the event was handled successfully, false otherwise.
 */
static bool bad_usb_app_back_event_callback(void* context) {
    furi_assert(context);
    BadUsbApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}


/**
 * @brief Tick event callback for the Bad USB application.
 *
 * This function is called periodically to handle tick events in the Bad USB application.
 * It asserts that the context is valid, casts the context to a BadUsbApp instance,
 * and then handles the tick event using the scene manager.
 *
 * @param context Pointer to the context, expected to be a BadUsbApp instance.
 */
static void bad_usb_app_tick_event_callback(void* context) {
    furi_assert(context);
    BadUsbApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/**
 * @brief Loads the settings for the Bad USB application.
 *
 * This function opens the storage record and allocates a FlipperFormat file object.
 * It attempts to open the settings file specified by BAD_USB_SETTINGS_PATH. If the file
 * exists and is valid, it reads the settings including the keyboard layout and interface type.
 * If the settings file is not valid or does not exist, it sets default values for the settings.
 *
 * @param app Pointer to the BadUsbApp instance whose settings are to be loaded.
 */
static void bad_usb_load_settings(BadUsbApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    bool state = false;

    FuriString* temp_str = furi_string_alloc();
    uint32_t version = 0;
    uint32_t interface = 0;

    if(flipper_format_file_open_existing(fff, BAD_USB_SETTINGS_PATH)) {
        do {
            if(!flipper_format_read_header(fff, temp_str, &version)) break;
            if((strcmp(furi_string_get_cstr(temp_str), BAD_USB_SETTINGS_FILE_TYPE) != 0) ||
               (version != BAD_USB_SETTINGS_VERSION))
                break;

            if(!flipper_format_read_string(fff, "layout", temp_str)) break;
            if(!flipper_format_read_uint32(fff, "interface", &interface, 1)) break;
            if(interface > BadUsbHidInterfaceBle) break;

            state = true;
        } while(0);
    }
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);

    if(state) {
        furi_string_set(app->keyboard_layout, temp_str);
        app->interface = interface;

        Storage* fs_api = furi_record_open(RECORD_STORAGE);
        FileInfo layout_file_info;
        FS_Error file_check_err = storage_common_stat(
            fs_api, furi_string_get_cstr(app->keyboard_layout), &layout_file_info);
        furi_record_close(RECORD_STORAGE);
        if((file_check_err != FSE_OK) || (layout_file_info.size != 256)) {
            furi_string_set(app->keyboard_layout, BAD_USB_SETTINGS_DEFAULT_LAYOUT);
        }
    } else {
        furi_string_set(app->keyboard_layout, BAD_USB_SETTINGS_DEFAULT_LAYOUT);
        app->interface = BadUsbHidInterfaceUsb;
    }

    furi_string_free(temp_str);
}

/**
 * @brief Saves the current settings of the BadUsbApp instance to a file.
 *
 * This function opens the storage record and allocates a FlipperFormat file object.
 * It then attempts to open or create the settings file specified by BAD_USB_SETTINGS_PATH.
 * If successful, it writes the settings header, keyboard layout, and interface type to the file.
 * Finally, it frees the FlipperFormat object and closes the storage record.
 *
 * @param app A pointer to the BadUsbApp instance whose settings are to be saved.
 */
static void bad_usb_save_settings(BadUsbApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_always(fff, BAD_USB_SETTINGS_PATH)) {
        do {
            if(!flipper_format_write_header_cstr(
                   fff, BAD_USB_SETTINGS_FILE_TYPE, BAD_USB_SETTINGS_VERSION))
                break;
            if(!flipper_format_write_string(fff, "layout", app->keyboard_layout)) break;
            uint32_t interface_id = app->interface;
            if(!flipper_format_write_uint32(fff, "interface", (const uint32_t*)&interface_id, 1))
                break;
        } while(0);
    }

    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
}

/**
 * @brief Allocates and initializes a new BadUsbApp instance.
 *
 * This function allocates memory for a new BadUsbApp instance and initializes its members.
 * It sets up the file path and keyboard layout, loads settings, and initializes various
 * components such as the GUI, notifications, dialogs, view dispatcher, scene manager, and
 * custom widgets. It also handles USB configuration and scene management based on the
 * provided argument.
 *
 * @param arg A string argument that specifies the file path for the BadUsbApp instance.
 *            If the argument is NULL or empty, the default file path is used.
 * @return A pointer to the newly allocated and initialized BadUsbApp instance.
 */
BadUsbApp* bad_usb_app_alloc(char* arg) {
    BadUsbApp* app = malloc(sizeof(BadUsbApp));

    app->bad_usb_script = NULL;

    app->file_path = furi_string_alloc();
    app->keyboard_layout = furi_string_alloc();
    if(arg && strlen(arg)) {
        furi_string_set(app->file_path, arg);
    }

    bad_usb_load_settings(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);

    app->scene_manager = scene_manager_alloc(&bad_usb_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, bad_usb_app_tick_event_callback, 500);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, bad_usb_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, bad_usb_app_back_event_callback);

    // Custom Widget
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BadUsbAppViewError, widget_get_view(app->widget));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        BadUsbAppViewConfig,
        variable_item_list_get_view(app->var_item_list));

    app->bad_usb_view = bad_usb_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BadUsbAppViewWork, bad_usb_view_get_view(app->bad_usb_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(furi_hal_usb_is_locked()) {
        app->error = BadUsbAppErrorCloseRpc;
        app->usb_if_prev = NULL;
        scene_manager_next_scene(app->scene_manager, BadUsbSceneError);
    } else {
        app->usb_if_prev = furi_hal_usb_get_config();
        furi_check(furi_hal_usb_set_config(NULL, NULL));

        if(!furi_string_empty(app->file_path)) {
            scene_manager_next_scene(app->scene_manager, BadUsbSceneWork);
        } else {
            furi_string_set(app->file_path, BAD_USB_APP_BASE_FOLDER);
            scene_manager_next_scene(app->scene_manager, BadUsbSceneFileSelect);
        }
    }

    return app;
}

/**
 * @brief Frees the resources allocated for the BadUsbApp instance.
 *
 * This function releases all the resources associated with the BadUsbApp instance,
 * including views, widgets, configuration menus, and other dynamically allocated
 * memory. It also ensures that any open scripts are closed and settings are saved.
 *
 * @param app Pointer to the BadUsbApp instance to be freed.
 */
void bad_usb_app_free(BadUsbApp* app) {
    furi_assert(app);

    // Close the Bad USB script if it is open
    if(app->bad_usb_script) {
        bad_usb_script_close(app->bad_usb_script);
        app->bad_usb_script = NULL;
    }

    // Remove and free the views
    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewWork);
    bad_usb_view_free(app->bad_usb_view);

    // Remove and free the custom widget
    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewError);
    widget_free(app->widget);

    // Remove and free the configuration menu
    view_dispatcher_remove_view(app->view_dispatcher, BadUsbAppViewConfig);
    variable_item_list_free(app->var_item_list);

    // Free the view dispatcher and scene manager
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    // Close the records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);

    // Save the application settings
    bad_usb_save_settings(app);

    // Free the strings
    furi_string_free(app->file_path);
    furi_string_free(app->keyboard_layout);

    // Restore the previous USB configuration if it was changed
    if(app->usb_if_prev) {
        furi_check(furi_hal_usb_set_config(app->usb_if_prev, NULL));
    }

    // Free the application instance
    free(app);
}

/**
 * @brief Entry point for the Bad USB application.
 *
 * This function initializes the Bad USB application, runs the view dispatcher,
 * and then frees the application resources upon completion.
 *
 * @param p Pointer to the parameter passed to the application.
 * @return int32_t Returns 0 upon successful execution.
 */
int32_t bad_usb_app(void* p) {
    // Allocate and initialize the Bad USB application
    BadUsbApp* bad_usb_app = bad_usb_app_alloc((char*)p);

    // Run the view dispatcher for the application
    view_dispatcher_run(bad_usb_app->view_dispatcher);

    // Free the application resources
    bad_usb_app_free(bad_usb_app);

    // Return success
    return 0;
}

