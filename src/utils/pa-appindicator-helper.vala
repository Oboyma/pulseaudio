using AppIndicator;

[DBus (name = "org.PulseAudio.AppIndicatorHelper")]
public class AppIndicatorHelper : GLib.Object {
    private Indicator indicator;
    private HashTable<string, SList<Gtk.MenuItem>> actions;

    public AppIndicatorHelper(Indicator indicator) {
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

public class PAAppIndicatorHelper : Gtk.Application {
    public PAAppIndicatorHelper() {
        Object(application_id: "org.PulseAudio.AppIndicatorHelper", inactivity_timeout: 0);
    }

    public override bool local_command_line(ref unowned string[] args, out int exit_code) {
        try {
            this.register();
        } catch (GLib.Error e) {
            stderr.printf("%s\n", e.message);

            exit_code = 1;
            return true;
        }

        if (strv_length(args) > 1 && args[1] == "--start") {
            if (!this.is_remote)
                register_helper();
        } else if (strv_length(args) > 1 && args[1] == "--stop") {
            if (!this.is_remote)
                stderr.printf("Not running.\n");

            this.activate();
        } else {
            stderr.printf("Usage: --start or --stop\n");

            if (!this.is_remote)
               this.activate();
        }

        exit_code = 0;
        return true;
    }

    public override void startup() {
        base.startup();
        this.hold();

        this.activate.connect(this.release);
    }

    private void register_helper() {
        var indicator = new Indicator("PulseAudio", "audio-card-symbolic", IndicatorCategory.HARDWARE);

        indicator.set_status(IndicatorStatus.PASSIVE);

        var menu = new Gtk.Menu();
        indicator.set_menu(menu);

        try {
            var conn = Bus.get_sync(BusType.SESSION);
            conn.register_object ("/org/PulseAudio/AppIndicatorHelper", new AppIndicatorHelper(indicator));
        } catch (IOError e) {
            this.activate();
        }
    }
}

public static int main(string[] args) {
    if (args.length > 1 && args[1] == "--start") {
        uint pid = Posix.fork();

        if (pid != 0)
            return 0;
    }

    return new PAAppIndicatorHelper().run(args);
}
