import sys
from pathlib import Path

header = Path(sys.argv[1])
text = header.read_text(encoding="utf-8")
text = text.replace(
    "  ConstRef<T> increment = (max() - min()) / 20;",
    "  ConstRef<T> increment = T(5);  // patched for MSVC 14.38 ICE",
)
header.write_text(text, encoding="utf-8")
print(f"Patched {header}")
