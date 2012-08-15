using Gtk;
using AppIndicator;

[DBus (name = "org.PulseAudio.AppIndicatorServer")]
public class AppIndicatorServer : GLib.Object {
    private Indicator indicator;
    private HashTable<string, SList<Gtk.MenuItem>> actions;

    public AppIndicatorServer(Indicator indicator) {
        this.indicator = indicator;
        this.actions = new HashTable<string, SList<Gtk.MenuItem>>(str_hash, str_equal);
    }

    public void show_actions(string title, HashTable<string,string> actions) {
        if (actions.size() > 0) {
            var items = new SList<Gtk.MenuItem>();
            var menu = indicator.get_menu();

            var menu_item = new Gtk.MenuItem.with_label(title);
            menu_item.sensitive = false;
            menu_item.show();
            menu.append(menu_item);
            items.prepend(menu_item);

            actions.foreach((key, label) => {
                menu_item = new Gtk.MenuItem.with_label(label);

                menu_item.activate.connect(() => {
                    clear_actions(title);
                    action_invoked(title, key);
                });

                menu_item.show();
                menu.append(menu_item);
                items.prepend(menu_item);
            });

            if (this.actions.contains(title)) {
                remove_items(title);
            }

            this.actions.replace(title, (owned) items);
            this.indicator.set_status(IndicatorStatus.ACTIVE);
        }
    }

    public void clear_actions(string title) {
        remove_items(title);
        this.actions.remove(title);

        if (this.actions.size() == 0) {
            this.indicator.set_status(IndicatorStatus.PASSIVE);
        }
    }

    public void clear_all_actions() {
        this.actions.foreach((key, items) => {
            remove_items(key);
        });

        this.actions.remove_all();

        this.indicator.set_status(IndicatorStatus.PASSIVE);
    }

    private void remove_items(string title) {
        var menu = indicator.get_menu();

        if (this.actions.contains(title)) {
            unowned SList<Gtk.MenuItem> items = this.actions.get(title);

            items.foreach(menu_item => {
                menu.remove(menu_item);
            });
        }
    }

    public signal void action_invoked(string title, string action_key);
}

public static int main(string[] args) {
    Gtk.init(ref args);
    var indicator = new Indicator("PulseAudio", "audio-card-symbolic", IndicatorCategory.HARDWARE);

    indicator.set_status(IndicatorStatus.PASSIVE);

    var menu = new Gtk.Menu();
    indicator.set_menu(menu);

    Bus.own_name(BusType.SESSION, "org.PulseAudio.AppIndicatorServer", BusNameOwnerFlags.NONE,
        (conn) => {
            try {
                conn.register_object ("/org/PulseAudio/AppIndicatorServer", new AppIndicatorServer(indicator));
            } catch (IOError e) {
                stderr.printf ("Could not register service\n");
            }
        },
        () => {},
        Gtk.main_quit
    );

    Gtk.main();

    return 0;
}
