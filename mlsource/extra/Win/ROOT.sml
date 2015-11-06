PolyML.Compiler.reportUnreferencedIds := true;
use "Base";
use "Globals";
use "LocaleBase";
use "Locale";
use "Resource";
use "WinBase";
use "ComboBase";
use "ScrollBase";
use "MenuBase";
use "MessageBase";
use "Message"; (* Quite a bit to do here. *)
use "Class";
use "FlagPrint";
use "Window";
use "ComboBox";
use "Edit";
use "ListBox";
use "Scrollbar";
use "Static";
use "Button";
use "Dialog";
use "Cursor";
use "Icon";
use "Menu";
use "GdiBase";
use "Color";
use "FontBase";
use "Font";
use "DeviceBase"; (* Currently working on this *)
use "Bitmap";
use "Brush";
use "Pen";
use "DeviceContext";
use "CommonDialog";
use "MessageBox";
use "Caret";
use "Transform";
use "Metafile";
use "Clipboard";
use "Rectangle";
use "Printing";
use "Line";
use "Path";
use "Region";
use "Painting";
use "Shape";
use "Clipping";
use "DragDrop";
use "Keyboard";
use "Mouse";
use "WinSystem";
use "CommonControls";

List.app PolyML.Compiler.forgetStructure
   [ "Base", "ComboBase", "FontBase", "MenuBase", "ScrollBase", "WinBase",
     "MessageBase", "GdiBase", "DeviceBase", "LocaleBase" ];

List.app PolyML.Compiler.forgetFunctor ["FlagPrint"];
