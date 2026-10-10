using System.Windows;

namespace AcemagicS1Control;

public partial class MainWindow : Window
{
    private readonly ViewModels.MainViewModel _vm = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        Closing += (_, _) => _vm.Dispose();
    }
}
