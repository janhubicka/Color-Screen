from pathlib import Path

h = Path('src/qtgui/ParameterPanel.h')
text = h.read_text()
text = text.replace('#include <QList>\n', '')
old = '''  // Qt 6.11 requires Q_OBJECT for QObject::findChildren<T>(). Lightweight
  // implementation-only helpers such as DetachableSection intentionally do not
  // need meta-object data, so filter QWidget children with C++ RTTI instead.
  template <typename T> QList<T> findChildren() const {
    QList<T> matches;
    for (QWidget *child : QObject::findChildren<QWidget *>()) {
      if (T typedChild = dynamic_cast<T>(child))
        matches.append(typedChild);
    }
    return matches;
  }

'''
if old not in text:
    raise SystemExit('ParameterPanel findChildren helper not found')
text = text.replace(old, '''  // Host propagation must not depend on QObject parenting: layouts can reparent
  // detachable sections as inspectors move between presentations.
  std::vector<std::function<void(QMainWindow *)>> m_detachableHostUpdaters;

''')
h.write_text(text)

cpp = Path('src/qtgui/ParameterPanel.cpp')
text = cpp.read_text()
old = '''void ParameterPanel::setDetachableHost(QMainWindow *host) {
  m_detachableHost = host;
  for (DetachableSection *section : findChildren<DetachableSection *>())
    section->setHost(host);
}
'''
if old not in text:
    raise SystemExit('setDetachableHost implementation not found')
text = text.replace(old, '''void ParameterPanel::setDetachableHost(QMainWindow *host) {
  m_detachableHost = host;
  for (const auto &updateHost : m_detachableHostUpdaters)
    updateHost(host);
}
''')
old = '''  return new DetachableSection(title, content, std::move(beforeDetach),
                               m_detachableHost.data());
'''
if old not in text:
    raise SystemExit('createDetachableSection implementation not found')
text = text.replace(old, '''  auto *section = new DetachableSection(title, content, std::move(beforeDetach),
                                        m_detachableHost.data(), this);
  QPointer<DetachableSection> guardedSection(section);
  m_detachableHostUpdaters.push_back(
      [guardedSection](QMainWindow *host) {
        if (guardedSection)
          guardedSection->setHost(host);
      });
  return section;
''')
cpp.write_text(text)
