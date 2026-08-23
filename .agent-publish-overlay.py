from pathlib import Path


def replace(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"expected text not found in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, count))


replace("src/qtgui/ImageWidget.cpp",
        "#include <QTimer>\n#include <QtMath>\n",
        "#include <QTimer>\n#include <QtMath>\n\n#include <cmath>\n")

replace("src/qtgui/ImageWidget.cpp",
        """        QPainter p (&overlay);
        p.setRenderHint (QPainter::Antialiasing);

        colorscreen::scr_to_img map;
        if (!map.set_parameters (scrToImg, *scan)) {
          *result = std::move (overlay);
          return;
        }

        /* Background-safe equivalent of imageToWidget.
""",
        """        colorscreen::scr_to_img map;
        if (!map.set_parameters (scrToImg, *scan)) {
          *result = std::move (overlay);
          return;
        }

        QPainter p (&overlay);
        p.setRenderHint (QPainter::Antialiasing);

        /* Background-safe equivalent of imageToWidget.
""")

replace("NEWS",
        """     safely cull invalid or far-off simulated targets in this coordinate mode,
     avoiding rasterizer stalls on pathological residuals. Views can still be detached
""",
        """     safely cull invalid or far-off simulated targets in this coordinate mode,
     avoiding rasterizer stalls on pathological residuals. Views can still be
     detached
""")
