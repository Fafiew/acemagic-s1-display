using System;
using System.Windows;

namespace AcemagicS1Control;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        // Single-instance guard (a second copy just focuses the first).
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            MessageBox.Show($"Unhandled error: {args.ExceptionObject}",
                "ACEMAGIC S1 Control", MessageBoxButton.OK, MessageBoxImage.Error);
        };
    }
}
