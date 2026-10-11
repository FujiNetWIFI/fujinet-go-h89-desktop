/*
 * DebuggerWindow -- the debugger (Qt6 Widgets) over the session's engine,
 * core/include/h89debug.h: the GNOME window's tabs and toolbar.
 *
 * Tabs: a Prompt (the engine's commands, Tab completion, a history), the
 * Z80 and memory, the disassembly (click a line for a breakpoint),
 * breakpoints, and the H89's parts as the engine reports them. The toolbar
 * steps, picks the radix (hex, octal, Heath's split octal), loads symbols
 * and saves. F5 run/stop, F7 step, F8 over, Shift+F8 out, F12 close.
 *
 * Showing it attaches the engine (which stops the machine); hiding it
 * detaches and lets the machine run on.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DebuggerWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstring>
#include <vector>

#include "../FujiNetWindows.h"
#include "h89debug.h"

namespace {

constexpr int kDisasmRows = 48;
constexpr int kMemRows = 32;

struct RegDef {
    const char *name;
    int reg;
    int wide;   /* 1 a word, 0 a byte, -1 a small number */
};
const RegDef kRegs[] = {
    { "PC", H89_REG_PC, 1 }, { "SP", H89_REG_SP, 1 }, { "AF", H89_REG_AF, 1 },
    { "BC", H89_REG_BC, 1 }, { "DE", H89_REG_DE, 1 }, { "HL", H89_REG_HL, 1 },
    { "IX", H89_REG_IX, 1 }, { "IY", H89_REG_IY, 1 }, { "AF'", H89_REG_AF2, 1 },
    { "BC'", H89_REG_BC2, 1 }, { "DE'", H89_REG_DE2, 1 }, { "HL'", H89_REG_HL2, 1 },
    { "I", H89_REG_I, 0 }, { "R", H89_REG_R, 0 }, { "IM", H89_REG_IM, -1 },
    { "IFF1", H89_REG_IFF1, -1 }, { "IFF2", H89_REG_IFF2, -1 },
};
constexpr int kNRegs = sizeof kRegs / sizeof kRegs[0];

QFont mono()
{
    QFont f(QStringLiteral("monospace"));
    f.setStyleHint(QFont::TypeWriter);
    return f;
}

class Window : public QWidget {
public:
    Window(h89session *session, QWidget *parent);
    void attachAndRefresh();
    void hideAndDetach();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void refreshAll();
    void refreshStatus();
    void refreshCpu();
    void refreshMem();
    void refreshDisasm();
    void refreshBps();
    void refreshH89();
    void tick();
    bool parse(const QString &text, long *out);
    void say(const QString &msg);
    void runPrompt();

    h89session *m_session;
    h89debug *m_dbg;
    unsigned m_seen = 0;
    bool m_wasStopped = false;
    int m_runningTicks = 0;
    QTimer m_timer;

    QLabel *m_status = nullptr;
    QPushButton *m_run = nullptr;
    QComboBox *m_radix = nullptr;
    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_promptOut = nullptr;
    QLineEdit *m_promptIn = nullptr;
    QStringList m_history;
    int m_historyPos = 0;
    QLineEdit *m_reg[kNRegs] = {};
    QCheckBox *m_flag[6] = {};
    QLabel *m_cpuInfo = nullptr;
    QPlainTextEdit *m_mem = nullptr;
    QLineEdit *m_memGoto = nullptr, *m_memAddr = nullptr, *m_memVal = nullptr;
    int m_memBase = 0x2000;
    bool m_updatingFlags = false;
    QPlainTextEdit *m_disasm = nullptr;
    QCheckBox *m_follow = nullptr;
    QLineEdit *m_jump = nullptr;
    int m_disasmTop = 0;
    uint16_t m_lineAddr[kDisasmRows] = {};
    int m_lineCount = 0;
    QListWidget *m_bps = nullptr;
    QComboBox *m_bpType = nullptr;
    QLineEdit *m_bpStart = nullptr, *m_bpEnd = nullptr, *m_bpCond = nullptr;
    QLabel *m_bpMsg = nullptr;
    QPlainTextEdit *m_h89 = nullptr;
};

QPointer<Window> g_window;

Window::Window(h89session *session, QWidget *parent)
    : QWidget(parent, Qt::Window), m_session(session), m_dbg(h89session_debugger(session))
{
    setWindowTitle(QStringLiteral("Debugger"));
    resize(980, 720);
    auto *outer = new QVBoxLayout(this);

    /* toolbar */
    auto *bar = new QHBoxLayout;
    m_run = new QPushButton(QStringLiteral("Stop (F5)"));
    connect(m_run, &QPushButton::clicked, this, [this] {
        if (h89debug_is_stopped(m_dbg)) h89debug_resume(m_dbg); else h89debug_stop(m_dbg);
        refreshAll();
    });
    bar->addWidget(m_run);
    auto add = [&](const char *label, const char *tip, std::function<void()> fn) {
        auto *b = new QPushButton(QString::fromUtf8(label));
        b->setToolTip(QString::fromUtf8(tip));
        connect(b, &QPushButton::clicked, this, [this, fn] { fn(); refreshAll(); });
        bar->addWidget(b);
    };
    add("Step (F7)", "One instruction", [this] { h89debug_step(m_dbg); });
    add("Over (F8)", "Over a CALL, RST, DJNZ or block op", [this] { h89debug_step_over(m_dbg); });
    add("Out (Shift+F8)", "To the return", [this] { h89debug_step_out(m_dbg); });
    add("Tick", "One 2 ms clock tick", [this] { h89debug_ticks(m_dbg, 1); });
    m_radix = new QComboBox;
    m_radix->addItems({ QStringLiteral("Hex"), QStringLiteral("Octal"), QStringLiteral("Split octal") });
    m_radix->setToolTip(QStringLiteral("How numbers are shown: hex, octal, or Heath's split octal (041.000)"));
    m_radix->setCurrentIndex(h89debug_radix(m_dbg));
    connect(m_radix, &QComboBox::currentIndexChanged, this, [this](int r) {
        h89debug_set_radix(m_dbg, r);
        h89session_set_int(m_session, "debugger.radix", r);
        refreshAll();
    });
    bar->addWidget(m_radix);
    bar->addStretch(1);
    auto *syms = new QPushButton(QStringLiteral("Symbols..."));
    connect(syms, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Load Symbols"), QString(),
            QStringLiteral("Symbol files (*.map *.sym *.noi *.txt);;All files (*)"));
        if (f.isEmpty()) return;
        char msg[512];
        h89debug_load_symbols(m_dbg, f.toLocal8Bit().constData(), msg, sizeof msg);
        say(QString::fromUtf8(msg));
        refreshAll();
    });
    bar->addWidget(syms);
    auto *save = new QToolButton;
    save->setText(QStringLiteral("Save"));
    save->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(save);
    struct SaveDef { const char *kind, *title, *file; };
    static const SaveDef saves[] = {
        { "dis", "Disassembly (the 64K as mapped)", "disassembly.asm" },
        { "ram", "Memory (the 64K as the CPU sees it)", "memory.bin" },
        { "regs", "Registers and the H89's state", "h89-state.txt" },
    };
    for (const SaveDef &s : saves) {
        const char *kind = s.kind;
        const char *file = s.file;
        menu->addAction(QString::fromUtf8(s.title), this, [this, kind, file] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Save"), QString::fromUtf8(file));
            if (f.isEmpty()) return;
            char msg[512];
            h89debug_save(m_dbg, kind, f.toLocal8Bit().constData(), msg, sizeof msg);
            say(QString::fromUtf8(msg));
        });
    }
    save->setMenu(menu);
    bar->addWidget(save);
    outer->addLayout(bar);
    m_status = new QLabel;
    outer->addWidget(m_status);

    m_tabs = new QTabWidget;
    outer->addWidget(m_tabs, 1);

    /* Prompt */
    {
        auto *page = new QWidget;
        auto *l = new QVBoxLayout(page);
        m_promptOut = new QPlainTextEdit;
        m_promptOut->setReadOnly(true);
        m_promptOut->setFont(mono());
        m_promptOut->setPlainText(QStringLiteral("FujiNet Go H89 debugger. Type help for the commands."));
        m_promptIn = new QLineEdit;
        m_promptIn->setFont(mono());
        m_promptIn->setPlaceholderText(QStringLiteral("help, step, break 040.000, mem hl, radix split, fdc ..."));
        m_promptIn->installEventFilter(this);
        connect(m_promptIn, &QLineEdit::returnPressed, this, &Window::runPrompt);
        l->addWidget(m_promptOut, 1);
        l->addWidget(m_promptIn);
        m_tabs->addTab(page, QStringLiteral("Prompt"));
    }
    /* CPU & Memory */
    {
        auto *page = new QWidget;
        auto *l = new QVBoxLayout(page);
        auto *grid = new QGridLayout;
        for (int i = 0; i < kNRegs; ++i) {
            m_reg[i] = new QLineEdit;
            m_reg[i]->setFont(mono());
            m_reg[i]->setMaximumWidth(110);
            const int reg = kRegs[i].reg;
            QLineEdit *e = m_reg[i];
            connect(e, &QLineEdit::returnPressed, this, [this, e, reg] {
                long v;
                if (parse(e->text(), &v)) h89debug_cpu_set(m_dbg, reg, (int)v);
                refreshAll();
            });
            grid->addWidget(new QLabel(QString::fromUtf8(kRegs[i].name)), i / 6, (i % 6) * 2);
            grid->addWidget(m_reg[i], i / 6, (i % 6) * 2 + 1);
        }
        l->addLayout(grid);
        auto *flags = new QHBoxLayout;
        flags->addWidget(new QLabel(QStringLiteral("Flags")));
        static const char *const names[6] = { "S", "Z", "H", "P/V", "N", "C" };
        static const int regs[6] = { H89_FLAG_S, H89_FLAG_Z, H89_FLAG_H, H89_FLAG_PV, H89_FLAG_N, H89_FLAG_C };
        for (int i = 0; i < 6; ++i) {
            m_flag[i] = new QCheckBox(QString::fromUtf8(names[i]));
            const int reg = regs[i];
            connect(m_flag[i], &QCheckBox::toggled, this, [this, reg](bool on) {
                if (!m_updatingFlags && h89debug_is_stopped(m_dbg)) h89debug_cpu_set(m_dbg, reg, on);
            });
            flags->addWidget(m_flag[i]);
        }
        flags->addStretch(1);
        l->addLayout(flags);
        m_cpuInfo = new QLabel;
        m_cpuInfo->setFont(mono());
        l->addWidget(m_cpuInfo);
        auto *row = new QHBoxLayout;
        m_memGoto = new QLineEdit;
        m_memGoto->setPlaceholderText(QStringLiteral("address, label or expression"));
        m_memAddr = new QLineEdit;
        m_memVal = new QLineEdit;
        m_memAddr->setMaximumWidth(110);
        m_memVal->setMaximumWidth(70);
        connect(m_memGoto, &QLineEdit::returnPressed, this, [this] {
            long a;
            if (parse(m_memGoto->text(), &a)) m_memBase = (int)(a & 0xfff0);
            refreshMem();
        });
        connect(m_memVal, &QLineEdit::returnPressed, this, [this] {
            long a, v;
            if (!parse(m_memAddr->text(), &a) || !parse(m_memVal->text(), &v)) {
                say(QStringLiteral("An address and a value, please"));
                return;
            }
            h89debug_write(m_dbg, (uint16_t)a, (uint8_t)v);
            refreshAll();
        });
        row->addWidget(new QLabel(QStringLiteral("Memory at")));
        row->addWidget(m_memGoto, 1);
        row->addWidget(new QLabel(QStringLiteral("Write")));
        row->addWidget(m_memAddr);
        row->addWidget(new QLabel(QStringLiteral("=")));
        row->addWidget(m_memVal);
        l->addLayout(row);
        m_mem = new QPlainTextEdit;
        m_mem->setReadOnly(true);
        m_mem->setFont(mono());
        m_mem->viewport()->installEventFilter(this);
        l->addWidget(m_mem, 1);
        m_tabs->addTab(page, QStringLiteral("CPU && Memory"));
    }
    /* Disassembly */
    {
        auto *page = new QWidget;
        auto *l = new QVBoxLayout(page);
        auto *row = new QHBoxLayout;
        m_follow = new QCheckBox(QStringLiteral("Follow PC"));
        m_follow->setChecked(true);
        connect(m_follow, &QCheckBox::toggled, this, [this] { refreshDisasm(); });
        m_jump = new QLineEdit;
        m_jump->setPlaceholderText(QStringLiteral("go to an address or a label"));
        connect(m_jump, &QLineEdit::returnPressed, this, [this] {
            long a;
            if (!parse(m_jump->text(), &a)) return;
            m_follow->setChecked(false);
            m_disasmTop = (int)(a & 0xffff);
            refreshDisasm();
        });
        row->addWidget(m_follow);
        row->addWidget(m_jump, 1);
        auto *hint = new QLabel(QStringLiteral("Click a line to toggle its breakpoint; scroll to browse"));
        hint->setStyleSheet(QStringLiteral("color: gray;"));
        row->addWidget(hint);
        l->addLayout(row);
        m_disasm = new QPlainTextEdit;
        m_disasm->setReadOnly(true);
        m_disasm->setFont(mono());
        m_disasm->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_disasm->viewport()->installEventFilter(this);
        l->addWidget(m_disasm, 1);
        m_tabs->addTab(page, QStringLiteral("Disassembly"));
    }
    /* Breakpoints */
    {
        auto *page = new QWidget;
        auto *l = new QVBoxLayout(page);
        m_bps = new QListWidget;
        m_bps->setFont(mono());
        connect(m_bps, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
            const int id = it->data(Qt::UserRole).toInt();
            if (id > 0) {
                h89debug_breakpoint_enable(m_dbg, id, it->checkState() == Qt::Checked);
                refreshDisasm();
            }
        });
        l->addWidget(m_bps, 1);
        auto *row = new QHBoxLayout;
        m_bpType = new QComboBox;
        m_bpType->addItems({ QStringLiteral("Execute"), QStringLiteral("Read"), QStringLiteral("Write"),
                             QStringLiteral("Read/Write"), QStringLiteral("Port In"), QStringLiteral("Port Out") });
        m_bpStart = new QLineEdit;
        m_bpStart->setPlaceholderText(QStringLiteral("start"));
        m_bpEnd = new QLineEdit;
        m_bpEnd->setPlaceholderText(QStringLiteral("end (optional)"));
        m_bpCond = new QLineEdit;
        m_bpCond->setPlaceholderText(QStringLiteral("condition, e.g. a == 0 or value == 3"));
        auto *addBp = new QPushButton(QStringLiteral("Add"));
        auto *remove = new QPushButton(QStringLiteral("Remove"));
        auto *clear = new QPushButton(QStringLiteral("Clear All"));
        auto doAdd = [this] {
            static const int types[6] = { H89DEBUG_BP_EXEC, H89DEBUG_BP_READ, H89DEBUG_BP_WRITE,
                                          H89DEBUG_BP_READ | H89DEBUG_BP_WRITE, H89DEBUG_BP_IN,
                                          H89DEBUG_BP_OUT };
            long a, b;
            if (!parse(m_bpStart->text(), &a)) { m_bpMsg->setText(QStringLiteral("Not an address, a label or an expression")); return; }
            b = a;
            if (!m_bpEnd->text().isEmpty() && !parse(m_bpEnd->text(), &b)) { m_bpMsg->setText(QStringLiteral("The end is not an address")); return; }
            const int id = h89debug_breakpoint_add(m_dbg, types[m_bpType->currentIndex()], (uint16_t)a, (uint16_t)b,
                                                   m_bpCond->text().toUtf8().constData());
            if (id < 0) { m_bpMsg->setText(QStringLiteral("The condition does not parse")); return; }
            m_bpMsg->clear();
            m_bpStart->clear();
            m_bpEnd->clear();
            m_bpCond->clear();
            refreshAll();
        };
        connect(addBp, &QPushButton::clicked, this, doAdd);
        connect(m_bpCond, &QLineEdit::returnPressed, this, doAdd);
        connect(remove, &QPushButton::clicked, this, [this] {
            QListWidgetItem *it = m_bps->currentItem();
            if (it && it->data(Qt::UserRole).toInt() > 0)
                h89debug_breakpoint_remove(m_dbg, it->data(Qt::UserRole).toInt());
            refreshAll();
        });
        connect(clear, &QPushButton::clicked, this, [this] { h89debug_breakpoint_clear(m_dbg); refreshAll(); });
        row->addWidget(m_bpType);
        row->addWidget(m_bpStart);
        row->addWidget(m_bpEnd);
        row->addWidget(m_bpCond, 1);
        row->addWidget(addBp);
        row->addWidget(remove);
        row->addWidget(clear);
        l->addLayout(row);
        m_bpMsg = new QLabel;
        m_bpMsg->setStyleSheet(QStringLiteral("color: #d83b3b;"));
        l->addWidget(m_bpMsg);
        m_tabs->addTab(page, QStringLiteral("Breakpoints"));
    }
    /* H89 */
    m_h89 = new QPlainTextEdit;
    m_h89->setReadOnly(true);
    m_h89->setFont(mono());
    m_tabs->addTab(m_h89, QStringLiteral("H89"));

    {
        const QString tab = qEnvironmentVariable("H89_DEBUGGER_TAB");
        const char *const names[] = { "prompt", "cpu", "disassembly", "breakpoints", "h89" };
        for (int i = 0; i < 5; ++i)
            if (tab.compare(QLatin1String(names[i]), Qt::CaseInsensitive) == 0)
                m_tabs->setCurrentIndex(i);
    }
    /* the Prompt tab is for typing: its input has the keyboard whenever the
     * tab shows (otherwise the toolbar's first button would) */
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0) m_promptIn->setFocus();
    });
    if (m_tabs->currentIndex() == 0) m_promptIn->setFocus();
    connect(&m_timer, &QTimer::timeout, this, &Window::tick);
    m_timer.start(100);
}

bool Window::parse(const QString &text, long *out)
{
    char err[96];
    const QByteArray t = text.trimmed().toUtf8();
    return !t.isEmpty() && h89debug_eval(m_dbg, t.constData(), out, err, sizeof err) == 0;
}

void Window::say(const QString &msg)
{
    m_promptOut->appendPlainText(msg);
    m_status->setText(msg);
}

void Window::runPrompt()
{
    static char out[65536];
    const QString cmd = m_promptIn->text();
    if (cmd.isEmpty()) return;
    if (m_history.isEmpty() || m_history.last() != cmd) m_history << cmd;
    m_historyPos = m_history.size();
    m_promptOut->appendPlainText(QStringLiteral("> ") + cmd);
    h89debug_command(m_dbg, cmd.toUtf8().constData(), out, sizeof out);
    QString text = QString::fromUtf8(out);
    if (text.endsWith(QLatin1Char('\n'))) text.chop(1);
    if (!text.isEmpty()) m_promptOut->appendPlainText(text);
    m_promptIn->clear();
    if (m_radix->currentIndex() != h89debug_radix(m_dbg)) {
        QSignalBlocker b(m_radix);
        m_radix->setCurrentIndex(h89debug_radix(m_dbg));
    }
    refreshAll();
}

bool Window::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_promptIn && ev->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(ev);
        if (k->key() == Qt::Key_Up) {
            if (m_historyPos > 0) m_promptIn->setText(m_history.value(--m_historyPos));
            return true;
        }
        if (k->key() == Qt::Key_Down) {
            if (m_historyPos < m_history.size()) ++m_historyPos;
            m_promptIn->setText(m_history.value(m_historyPos));
            return true;
        }
        if (k->key() == Qt::Key_Tab) {
            char comps[4096];
            const QString text = m_promptIn->text();
            const int sp = text.lastIndexOf(QLatin1Char(' '));
            const int n = h89debug_completions(m_dbg, text.toUtf8().constData(), comps, sizeof comps);
            QString list = QString::fromUtf8(comps).trimmed();
            if (n == 1)
                m_promptIn->setText(text.left(sp + 1) + list + QLatin1Char(' '));
            else if (n > 1)
                m_promptOut->appendPlainText(list);
            return true;
        }
    }
    if (m_disasm && obj == m_disasm->viewport() && ev->type() == QEvent::MouseButtonPress) {
        auto *m = static_cast<QMouseEvent *>(ev);
        const int line = m_disasm->cursorForPosition(m->position().toPoint()).blockNumber();
        if (line >= 0 && line < m_lineCount) {
            h89debug_breakpoint_toggle(m_dbg, m_lineAddr[line]);
            refreshDisasm();
            refreshBps();
        }
        return true;
    }
    if (m_disasm && obj == m_disasm->viewport() && ev->type() == QEvent::Wheel) {
        auto *w = static_cast<QWheelEvent *>(ev);
        const int rows = w->angleDelta().y() > 0 ? -3 : 3;
        m_follow->setChecked(false);
        m_disasmTop = h89debug_row_address(m_dbg, (uint16_t)m_disasmTop, rows);
        refreshDisasm();
        return true;
    }
    if (m_mem && obj == m_mem->viewport() && ev->type() == QEvent::Wheel) {
        auto *w = static_cast<QWheelEvent *>(ev);
        const int per = h89debug_radix(m_dbg) == H89_RADIX_HEX ? 16 : 8;
        m_memBase = (m_memBase + (w->angleDelta().y() > 0 ? -4 : 4) * per) & 0xffff;
        refreshMem();
        return true;
    }
    return QWidget::eventFilter(obj, ev);
}

void Window::refreshStatus()
{
    char reason[160];
    int addr;
    const bool stopped = h89debug_is_stopped(m_dbg) != 0;
    h89debug_stop_reason(m_dbg, reason, sizeof reason, &addr);
    m_status->setText(stopped ? QStringLiteral("Stopped: %1").arg(QString::fromUtf8(reason))
                              : (h89debug_is_attached(m_dbg) ? QStringLiteral("Running")
                                                             : QStringLiteral("The machine is off")));
    m_run->setText(stopped ? QStringLiteral("Run (F5)") : QStringLiteral("Stop (F5)"));
    m_run->setStyleSheet(stopped ? QStringLiteral("background: %1; color: black;").arg(h89AccentColor().name())
                                 : QString());
}

void Window::refreshCpu()
{
    h89debug_cpu c;
    h89debug_cpu_get(m_dbg, &c);
    const int vals[kNRegs] = { c.pc, c.sp, c.af, c.bc, c.de, c.hl, c.ix, c.iy, c.af2, c.bc2,
                               c.de2, c.hl2, c.i, c.r, c.im, c.iff1, c.iff2 };
    for (int i = 0; i < kNRegs; ++i) {
        char buf[16];
        if (m_reg[i]->hasFocus() && !h89debug_is_stopped(m_dbg)) continue;
        if (kRegs[i].wide > 0) h89debug_fmt16(m_dbg, (unsigned)vals[i], buf, sizeof buf);
        else if (kRegs[i].wide == 0) h89debug_fmt8(m_dbg, (unsigned)vals[i], buf, sizeof buf);
        else snprintf(buf, sizeof buf, "%d", vals[i]);
        m_reg[i]->setText(QString::fromUtf8(buf));
    }
    const int flags[6] = { c.sf, c.zf, c.hf, c.pf, c.nf, c.cf };
    m_updatingFlags = true;
    for (int i = 0; i < 6; ++i) m_flag[i]->setChecked(flags[i] != 0);
    m_updatingFlags = false;
    m_cpuInfo->setText(QStringLiteral("%1 cycles   %2 ticks of the 2 ms clock%3")
                           .arg(c.cycles).arg(c.ticks).arg(c.halted ? QStringLiteral("   HALT") : QString()));
}

void Window::refreshMem()
{
    QString text;
    const int per = h89debug_radix(m_dbg) == H89_RADIX_HEX ? 16 : 8;
    for (int r = 0; r < kMemRows; ++r) {
        uint8_t row[16];
        char at[16], v[8];
        const uint16_t a = (uint16_t)(m_memBase + r * per);
        h89debug_read(m_dbg, a, row, per);
        QString line = QStringLiteral("%1 ").arg(QString::fromUtf8(h89debug_fmt16(m_dbg, a, at, sizeof at)), -8);
        for (int i = 0; i < per; ++i)
            line += QString::fromUtf8(h89debug_fmt8(m_dbg, row[i], v, sizeof v)) + QLatin1Char(' ');
        line += QLatin1Char(' ');
        for (int i = 0; i < per; ++i)
            line += QLatin1Char(row[i] >= 0x20 && row[i] < 0x7f ? (char)row[i] : '.');
        text += line + QLatin1Char('\n');
    }
    const int pos = m_mem->verticalScrollBar()->value();
    m_mem->setPlainText(text);
    m_mem->verticalScrollBar()->setValue(pos);
}

void Window::refreshDisasm()
{
    static h89debug_line lines[kDisasmRows];
    int pcLine = -1;
    if (m_follow->isChecked()) {
        h89debug_cpu c;
        h89debug_cpu_get(m_dbg, &c);
        m_disasmTop = h89debug_row_address(m_dbg, (uint16_t)c.pc, -(kDisasmRows / 3));
    }
    const int n = h89debug_disassemble(m_dbg, (uint16_t)m_disasmTop, lines, kDisasmRows, &pcLine);
    m_lineCount = n;
    QString text;
    for (int i = 0; i < n; ++i) {
        m_lineAddr[i] = lines[i].address;
        text += QString::asprintf("%c%c %-8s %-16s %-14s %s%s%s\n", lines[i].has_breakpoint ? '*' : ' ',
                                  lines[i].is_pc ? '>' : ' ', lines[i].addr_text, lines[i].bytes,
                                  lines[i].label, lines[i].disasm, lines[i].comment[0] ? "  ; " : "",
                                  lines[i].comment);
    }
    if (n == 0) text = QStringLiteral("(no disassembly)");
    m_disasm->setPlainText(text);
    QList<QTextEdit::ExtraSelection> sel;
    if (pcLine >= 0) {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(m_disasm->document()->findBlockByNumber(pcLine));
        s.format.setBackground(h89AccentColor());
        s.format.setForeground(Qt::black);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel << s;
    }
    m_disasm->setExtraSelections(sel);
}

void Window::refreshBps()
{
    h89debug_breakpoint bps[128];
    const int n = h89debug_breakpoint_list(m_dbg, bps, 128);
    QSignalBlocker block(m_bps);
    m_bps->clear();
    if (n == 0) {
        m_bps->addItem(QStringLiteral("No breakpoints. Click a disassembly line, or add one below."));
        return;
    }
    for (int i = 0; i < n; ++i) {
        char a[16], b[16];
        const bool port = (bps[i].type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) != 0;
        if (port) { h89debug_fmt8(m_dbg, bps[i].start, a, sizeof a); h89debug_fmt8(m_dbg, bps[i].end, b, sizeof b); }
        else { h89debug_fmt16(m_dbg, bps[i].start, a, sizeof a); h89debug_fmt16(m_dbg, bps[i].end, b, sizeof b); }
        const char *type = (bps[i].type & H89DEBUG_BP_EXEC) ? "Execute"
                         : (bps[i].type & H89DEBUG_BP_IN) ? "Port In"
                         : (bps[i].type & H89DEBUG_BP_OUT) ? "Port Out"
                         : ((bps[i].type & H89DEBUG_BP_READ) && (bps[i].type & H89DEBUG_BP_WRITE)) ? "Read/Write"
                         : (bps[i].type & H89DEBUG_BP_READ) ? "Read" : "Write";
        const QString range = bps[i].start == bps[i].end ? QString::fromUtf8(a)
                                                         : QStringLiteral("%1-%2").arg(QString::fromUtf8(a), QString::fromUtf8(b));
        auto *it = new QListWidgetItem(QString::asprintf("#%-3d %-10s %-16s hits %-6u %s%s", bps[i].id, type,
                                                         range.toUtf8().constData(), bps[i].hits,
                                                         bps[i].condition[0] ? "if " : "", bps[i].condition));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(bps[i].enabled ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, bps[i].id);
        m_bps->addItem(it);
    }
}

void Window::refreshH89()
{
    static char part[4096];
    QString text;
    for (int s = 0; s < H89DEBUG_SECTION_COUNT; ++s) {
        h89debug_section(m_dbg, s, part, sizeof part);
        text += (s ? QStringLiteral("\n") : QString()) + QString::fromUtf8(h89debug_section_name(s)) +
                QLatin1Char('\n') + QString::fromUtf8(part);
    }
    const int pos = m_h89->verticalScrollBar()->value();
    m_h89->setPlainText(text);
    m_h89->verticalScrollBar()->setValue(pos);
}

void Window::refreshAll()
{
    refreshStatus();
    refreshCpu();
    refreshMem();
    refreshDisasm();
    refreshBps();
    refreshH89();
}

void Window::tick()
{
    if (!isVisible()) return;
    if (!h89debug_is_attached(m_dbg) && h89session_is_running(m_session)) {
        attachAndRefresh();
        return;
    }
    const unsigned gen = h89debug_generation(m_dbg);
    const bool stopped = h89debug_is_stopped(m_dbg) != 0;
    if (gen != m_seen || stopped != m_wasStopped) {
        m_seen = gen;
        m_wasStopped = stopped;
        refreshAll();
    } else if (!stopped && ++m_runningTicks >= 5) {
        m_runningTicks = 0;
        refreshStatus();
        refreshCpu();
        refreshMem();
        refreshH89();
    }
}

void Window::attachAndRefresh()
{
    if (h89session_is_running(m_session))
        h89debug_attach(m_dbg);   /* stops the machine */
    m_seen = h89debug_generation(m_dbg) - 1;
    refreshAll();
}

void Window::hideAndDetach()
{
    h89debug_detach(m_dbg);
    hide();
}

void Window::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_F5:
        if (h89debug_is_stopped(m_dbg)) h89debug_resume(m_dbg); else h89debug_stop(m_dbg);
        refreshAll();
        return;
    case Qt::Key_F7: h89debug_step(m_dbg); refreshAll(); return;
    case Qt::Key_F8:
        if (e->modifiers() & Qt::ShiftModifier) h89debug_step_out(m_dbg); else h89debug_step_over(m_dbg);
        refreshAll();
        return;
    case Qt::Key_F12: hideAndDetach(); return;
    default: QWidget::keyPressEvent(e);
    }
}

void Window::closeEvent(QCloseEvent *e)
{
    h89debug_detach(m_dbg);
    QWidget::closeEvent(e);
}

} // namespace

void DebuggerWindow::showFor(QWidget *parent, h89session *session)
{
    if (!g_window) g_window = new Window(session, parent);
    g_window->show();
    g_window->raise();
    g_window->activateWindow();
    g_window->attachAndRefresh();
}

void DebuggerWindow::toggleFor(QWidget *parent, h89session *session)
{
    if (g_window && g_window->isVisible()) {
        g_window->hideAndDetach();
        return;
    }
    showFor(parent, session);
}
