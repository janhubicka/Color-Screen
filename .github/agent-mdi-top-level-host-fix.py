from pathlib import Path

main = Path('src/qtgui/main.cpp')
text = main.read_text()
marker = '''    if (!converted || duration <= 0)
      duration = 5000;
    qDebug() << "Smoke Test Mode: Will exit in" << duration << "ms...";
'''
replacement = '''    if (!converted || duration <= 0)
      duration = 5000;
    // New View and slanted-reference checks deliberately run serially because
    // both manipulate shared document presentation. Sanitizer builds,
    // especially ARM64 ASan, need more than the ordinary 30-second smoke
    // window; do not let the cleanup timer destroy their widgets mid-check.
    if (parser.isSet(newViewOption) && parser.isSet(slantedReferenceOption))
      duration = qMax(duration, 60000);
    qDebug() << "Smoke Test Mode: Will exit in" << duration << "ms...";
'''
if 'duration = qMax(duration, 60000);' not in text:
    if marker not in text:
        raise SystemExit('smoke shutdown timer block not found')
    text = text.replace(marker, replacement, 1)
main.write_text(text)
