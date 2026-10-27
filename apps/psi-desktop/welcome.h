#pragma once
#include <QWidget>

class QListWidget;

namespace psi {

// Start screen: Ψ logo, quick actions and recent projects.
class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);
    void refresh();

Q_SIGNALS:
    void newRequested();
    void openRequested(const QString& path);   // empty → show a file dialog
    void importDxfRequested();
    void demoRequested();

private:
    QListWidget* recent_ = nullptr;
};

} // namespace psi
