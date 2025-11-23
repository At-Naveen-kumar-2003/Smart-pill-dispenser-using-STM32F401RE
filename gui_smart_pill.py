import serial
import json
import tkinter as tk
from tkinter import messagebox, ttk, filedialog
from threading import Thread
import time
from datetime import datetime
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
import os
import csv

# ============================================
# CONFIGURATION
# ============================================
SERIAL_PORT = 'COM5'  # Change to your specific port
BAUD_RATE = 115200
DATA_FILE = "tablet_db.json"

# --- Email Configuration ---
EMAIL_CONFIG = {
    'enabled': True,
    'smtp_server': 'smtp.gmail.com',
    'smtp_port': 587,
    'sender_email': 'goat76305@gmail.com',
    'sender_password': 'zeyyhvmbkcgonhuy',  # Ensure this App Password is valid
    'recipient_primary': 'sanjaisaravanan8870@gmail.com',
    'recipient_secondary': '',
    'recipient_name': 'Caregiver',
}

# --- Global System State ---
TABLET_NAMES = ["Aspirin", "Vitamin C", "Calcium", "Multivitamin"]
TABLET_DETAILS = {}
ALARM_HISTORY = []

# --- Color Scheme ---
COLORS = {
    'primary': '#2C3E50', 'secondary': '#34495E', 'accent': '#3498DB',
    'success': '#27AE60', 'warning': '#F39C12', 'danger': '#E74C3C',
    'light': '#ECF0F1', 'white': '#FFFFFF', 'text_dark': '#2C3E50',
    'text_light': '#7F8C8D', 'bg_main': '#F5F6FA', 'unavailable': '#C0392B'
}

# --- Serial Setup ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    SERIAL_CONNECTED = True
    print(f"✓ Connected to {SERIAL_PORT} at {BAUD_RATE} bps.")
except (serial.SerialException, FileNotFoundError) as e:
    ser = None
    SERIAL_CONNECTED = False
    print(f"⚠ CONNECTION FAILED: {e}")


# ============================================
# UTILITY FUNCTIONS
# ============================================
def load_tablet_data():
    global TABLET_NAMES, TABLET_DETAILS
    if os.path.exists(DATA_FILE):
        try:
            with open(DATA_FILE, 'r') as f:
                data = json.load(f)
                saved_names = data.get('names', [])
                if saved_names:
                    TABLET_NAMES = saved_names
                TABLET_DETAILS = data.get('details', {})
        except Exception:
            pass

    # Initialize default details if missing
    for name in TABLET_NAMES:
        if name not in TABLET_DETAILS:
            TABLET_DETAILS[name] = {'dosage': '10mg', 'stock': 10}


def save_tablet_data():
    try:
        with open(DATA_FILE, 'w') as f:
            json.dump({'names': TABLET_NAMES, 'details': TABLET_DETAILS}, f, indent=4)
    except Exception:
        pass


def send_command(cmd):
    if SERIAL_CONNECTED and ser and ser.is_open:
        try:
            ser.write((cmd + '\n').encode('utf-8'))
            print(f"[TX] {cmd}")
        except Exception:
            pass
    else:
        print(f"[SIM TX] {cmd}")


def listen_for_data(app):
    if not SERIAL_CONNECTED or not ser:
        return
    while True:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[RX] {line}")
                    if line.startswith("{"):
                        try:
                            data = json.loads(line)
                            app.master.after(0, lambda d=data: app.handle_data(d))
                        except Exception:
                            pass
                    elif line.startswith("OK:") or line.startswith("ERROR:"):
                        app.master.after(0, lambda l=line: app.handle_data({'cmd': 'TEXT_MSG', 'msg': l}))
            time.sleep(0.01)
        except Exception:
            break


def convert_to_12hr(hour_24):
    am_pm = "AM" if hour_24 < 12 else "PM"
    hour = hour_24 % 12
    if hour == 0:
        hour = 12
    return hour, am_pm


# --- PROFESSIONAL EMAIL SENDER ---
def send_email_notification(subject, body_content, recipients=None):
    if not EMAIL_CONFIG['enabled']:
        return

    if recipients is None:
        recipients = [EMAIL_CONFIG['recipient_primary']]
        if EMAIL_CONFIG['recipient_secondary']:
            recipients.append(EMAIL_CONFIG['recipient_secondary'])

    Thread(target=_send_email_thread, args=(subject, body_content, recipients), daemon=True).start()


def _send_email_thread(subject, body_content, recipients):
    try:
        server = smtplib.SMTP(EMAIL_CONFIG['smtp_server'], EMAIL_CONFIG['smtp_port'])
        server.starttls()
        server.login(EMAIL_CONFIG['sender_email'], EMAIL_CONFIG['sender_password'])

        timestamp = datetime.now().strftime("%B %d, %Y at %I:%M %p")

        # Professional HTML Template
        html_template = f"""
        <html>
        <head>
            <style>
                body {{ font-family: 'Helvetica', 'Arial', sans-serif; background-color: #f8f9fa; margin: 0; padding: 20px; }}
                .container {{ max-width: 600px; margin: 0 auto; background-color: #ffffff; border-radius: 8px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); border-top: 6px solid #2C3E50; }}
                .header {{ padding: 25px; text-align: center; border-bottom: 1px solid #eeeeee; }}
                .header h2 {{ margin: 0; color: #2C3E50; font-size: 22px; }}
                .content {{ padding: 30px; color: #444444; line-height: 1.6; }}
                .alert-box {{ background-color: #FFF3CD; border-left: 5px solid #FFC107; color: #856404; padding: 15px; margin: 20px 0; border-radius: 4px; }}
                .footer {{ background-color: #f8f9fa; color: #6c757d; text-align: center; padding: 15px; font-size: 12px; border-top: 1px solid #eeeeee; }}
            </style>
        </head>
        <body>
            <div class="container">
                <div class="header">
                    <h2>Smart Dispenser Notification</h2>
                </div>
                <div class="content">
                    <p>Dear <strong>{EMAIL_CONFIG['recipient_name']}</strong>,</p>
                    {body_content}
                    <p style="margin-top:20px;">Please attend to this matter at your earliest convenience.</p>
                </div>
                <div class="footer">
                    <p>System Timestamp: {timestamp}</p>
                    <p>Automated Message - Do Not Reply</p>
                </div>
            </div>
        </body>
        </html>
        """

        for recipient in recipients:
            msg = MIMEMultipart()
            msg['From'] = EMAIL_CONFIG['sender_email']
            msg['To'] = recipient
            msg['Subject'] = subject
            msg.attach(MIMEText(html_template, 'html'))
            server.send_message(msg)

        server.quit()
        print(f"✓ Email sent to {len(recipients)} recipients")
    except Exception as e:
        print(f"✗ Email failed: {e}")


load_tablet_data()


# ============================================
# MAIN GUI CLASS
# ============================================
class PillDispenserGUI:
    def __init__(self, master):
        self.master = master
        master.title("Medical Pill Dispenser - Advanced Management System")
        master.geometry("1400x900")
        master.configure(bg=COLORS['bg_main'])

        self.alarms = {}
        self.connection_status = "connected" if SERIAL_CONNECTED else "offline"

        self.setup_styles()
        self.create_main_layout()

        if SERIAL_CONNECTED:
            self.update_connection_indicator()
            Thread(target=self.periodic_status_request, daemon=True).start()
            Thread(target=listen_for_data, args=(self,), daemon=True).start()
            # Staggered startup
            self.master.after(500, lambda: send_command("GET_TABLETS"))
            self.master.after(1000, lambda: send_command("GET_ALARMS"))
            self.master.after(1500, lambda: send_command("STATUS"))
        else:
            self.update_connection_indicator()
            self.log("System Started in OFFLINE mode.")

    def setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('Primary.TButton', background=COLORS['accent'], foreground='white', borderwidth=0,
                        font=('Segoe UI', 10, 'bold'), padding=(15, 8))
        style.map('Primary.TButton', background=[('active', '#2980B9')])
        style.configure('Success.TButton', background=COLORS['success'], foreground='white', borderwidth=0,
                        padding=(12, 6))
        style.map('Success.TButton', background=[('active', '#219955')])
        style.configure('Danger.TButton', background=COLORS['danger'], foreground='white', borderwidth=0,
                        padding=(10, 5))
        style.map('Danger.TButton', background=[('active', '#C0392B')])

    def create_main_layout(self):
        header = tk.Frame(self.master, bg=COLORS['primary'], height=70)
        header.pack(fill='x')
        tk.Label(header, text="🏥 MEDICAL PILL DISPENSER", font=('Segoe UI', 18, 'bold'), bg=COLORS['primary'],
                 fg='white').pack(side='left', padx=20, pady=15)
        self.connection_indicator = tk.Label(header, text="● CHECKING...", font=('Segoe UI', 11, 'bold'),
                                             bg=COLORS['primary'], fg='white')
        self.connection_indicator.pack(side='right', padx=20)

        self.notebook = ttk.Notebook(self.master)
        self.notebook.pack(fill='both', expand=True, padx=20, pady=20)

        self.create_dashboard_tab()
        self.create_alarm_tab()
        self.create_tablet_tab()
        self.create_history_tab()
        self.create_config_tab()
        self.create_log_tab()

    # --- TABS ---
    def create_dashboard_tab(self):
        tab = tk.Frame(self.notebook, bg=COLORS['bg_main'])
        self.notebook.add(tab, text="📊 Dashboard")

        left_col = tk.Frame(tab, bg=COLORS['bg_main'])
        left_col.pack(side='left', fill='both', expand=True, padx=10, pady=10)
        right_col = tk.Frame(tab, bg=COLORS['bg_main'])
        right_col.pack(side='right', fill='both', expand=True, padx=10, pady=10)

        card = self.create_card(left_col, "System Status", "⚡")
        card.pack(fill='x', pady=(0, 15))
        self.alert_banner = tk.Label(card, text="✓ SYSTEM READY", bg=COLORS['success'], fg='white',
                                     font=('Segoe UI', 12, 'bold'), pady=10)
        self.alert_banner.pack(fill='x')

        grid = tk.Frame(card, bg='white')
        grid.pack(fill='x', padx=10, pady=10)
        self.time_lbl = self.create_info_box(grid, "Current Time", "--:--:--", 0, 0)
        self.next_lbl = self.create_info_box(grid, "Next Medication", "Loading...", 0, 1)
        self.state_lbl = self.create_info_box(grid, "System State", "Idle", 1, 0)
        self.missed_lbl = self.create_info_box(grid, "Missed Count", "0", 1, 1)

        sched_card = self.create_card(right_col, "Today's Schedule", "📅")
        sched_card.pack(fill='both', expand=True)
        self.sched_canvas = tk.Canvas(sched_card, bg='white', highlightthickness=0)
        sb = ttk.Scrollbar(sched_card, command=self.sched_canvas.yview)
        self.sched_frame = tk.Frame(self.sched_canvas, bg='white')
        self.sched_canvas.create_window((0, 0), window=self.sched_frame, anchor='nw')
        self.sched_canvas.configure(yscrollcommand=sb.set)
        self.sched_canvas.pack(side='left', fill='both', expand=True, padx=10, pady=10)
        sb.pack(side='right', fill='y')
        self.sched_frame.bind('<Configure>',
                              lambda e: self.sched_canvas.configure(scrollregion=self.sched_canvas.bbox('all')))

    def create_alarm_tab(self):
        tab = tk.Frame(self.notebook, bg=COLORS['bg_main'])
        self.notebook.add(tab, text="⏰ Alarms")

        add_frame = self.create_card(tab, "Add Alarm (2 Min Gap Required)", "➕")
        add_frame.pack(fill='x', padx=10, pady=10)
        ctrls = tk.Frame(add_frame, bg='white')
        ctrls.pack(padx=10, pady=10)

        tk.Label(ctrls, text="Hour:", bg='white').pack(side='left')
        self.hour_var = tk.StringVar(value="08")
        tk.Entry(ctrls, textvariable=self.hour_var, width=5).pack(side='left', padx=5)

        tk.Label(ctrls, text="Min:", bg='white').pack(side='left')
        self.min_var = tk.StringVar(value="00")
        tk.Entry(ctrls, textvariable=self.min_var, width=5).pack(side='left', padx=5)

        tk.Label(ctrls, text="Med:", bg='white').pack(side='left')
        self.alarm_med_var = tk.StringVar()
        self.alarm_med_combo = ttk.Combobox(ctrls, textvariable=self.alarm_med_var, values=TABLET_NAMES,
                                            state='readonly')
        self.alarm_med_combo.pack(side='left', padx=5)
        if TABLET_NAMES:
            self.alarm_med_combo.current(0)

        ttk.Button(ctrls, text="Add", style='Success.TButton', command=self.add_alarm).pack(side='left', padx=10)

        list_card = self.create_card(tab, "Scheduled Alarms", "📋")
        list_card.pack(fill='both', expand=True, padx=10, pady=10)
        self.alarm_canvas = tk.Canvas(list_card, bg='white')
        self.alarm_list_frame = tk.Frame(self.alarm_canvas, bg='white')
        self.alarm_canvas.create_window((0, 0), window=self.alarm_list_frame, anchor='nw')
        self.alarm_canvas.pack(side='left', fill='both', expand=True)

    def create_tablet_tab(self):
        tab = tk.Frame(self.notebook, bg=COLORS['bg_main'])
        self.notebook.add(tab, text="💊 Tablets")

        edit_card = self.create_card(tab, "Manage Inventory", "✏")
        edit_card.pack(fill='x', padx=10, pady=10)
        f = tk.Frame(edit_card, bg='white')
        f.pack(padx=10, pady=10, fill='x')

        tk.Label(f, text="Name:", bg='white').pack(side='left')
        self.tab_name = tk.Entry(f)
        self.tab_name.pack(side='left', padx=5)
        tk.Label(f, text="Stock:", bg='white').pack(side='left')
        self.tab_stock = tk.Entry(f, width=5)
        self.tab_stock.pack(side='left', padx=5)
        self.tab_stock.insert(0, "10")
        ttk.Button(f, text="Save", style='Success.TButton', command=self.save_tablet).pack(side='left', padx=10)

        list_card = self.create_card(tab, "Inventory List", "📋")
        list_card.pack(fill='both', expand=True, padx=10, pady=10)
        cols = ('Name', 'Dosage', 'Stock')
        self.tab_tree = ttk.Treeview(list_card, columns=cols, show='headings')
        for c in cols:
            self.tab_tree.heading(c, text=c)
        self.tab_tree.pack(fill='both', expand=True, padx=10, pady=10)

        acts = tk.Frame(list_card, bg='white')
        acts.pack(fill='x', padx=10, pady=5)
        ttk.Button(acts, text="Refill (+10)", style='Primary.TButton', command=self.refill_tablet).pack(side='left')
        ttk.Button(acts, text="Delete", style='Danger.TButton', command=self.delete_tablet).pack(side='right')
        self.update_tablet_list_ui()

    def create_history_tab(self):
        tab = tk.Frame(self.notebook, bg=COLORS['bg_main'])
        self.notebook.add(tab, text="📜 History")
        ttk.Button(tab, text="Export CSV", command=self.export_csv).pack(pady=10)
        self.hist_tree = ttk.Treeview(tab, columns=('Time', 'Event', 'Detail'), show='headings')
        self.hist_tree.heading('Time', text='Time')
        self.hist_tree.heading('Event', text='Event')
        self.hist_tree.heading('Detail', text='Detail')
        self.hist_tree.pack(fill='both', expand=True, padx=10)

    def create_config_tab(self):
        tab = tk.Frame(self.notebook, bg=COLORS['bg_main'])
        self.notebook.add(tab, text="📧 Email")
        card = self.create_card(tab, "Configuration", "⚙")
        card.pack(fill='x', padx=10, pady=10)
        f = tk.Frame(card, bg='white')
        f.pack(padx=10, pady=10)
        tk.Label(f, text="Primary Email:", bg='white').grid(row=0, column=0)
        self.email1 = tk.Entry(f, width=30)
        self.email1.grid(row=0, column=1)
        self.email1.insert(0, EMAIL_CONFIG['recipient_primary'])
        ttk.Button(f, text="Save", command=self.save_email).grid(row=2, column=1, pady=10)

    def create_log_tab(self):
        tab = tk.Frame(self.notebook, bg=COLORS['bg_main'])
        self.notebook.add(tab, text="📝 Log")
        self.log_text = tk.Text(tab, bg='black', fg='white', font=('Consolas', 9))
        self.log_text.pack(fill='both', expand=True)

    def create_info_box(self, parent, title, val, r, c):
        f = tk.Frame(parent, bg=COLORS['light'], relief='solid', borderwidth=1)
        f.grid(row=r, column=c, sticky='ew', padx=5, pady=5, ipady=5)
        parent.columnconfigure(c, weight=1)
        tk.Label(f, text=title, bg=COLORS['light'], fg=COLORS['text_light'], font=('Segoe UI', 8)).pack(anchor='w',
                                                                                                        padx=10)
        lbl = tk.Label(f, text=val, bg=COLORS['light'], fg=COLORS['text_dark'], font=('Segoe UI', 11, 'bold'))
        lbl.pack(anchor='w', padx=10)
        return lbl

    def create_card(self, parent, title, icon):
        f = tk.Frame(parent, bg='white', relief='solid', borderwidth=1)
        tk.Label(f, text=f"{icon} {title}", bg=COLORS['secondary'], fg='white', font=('bold'), anchor='w',
                 padx=10).pack(fill='x')
        return f

    # ============================================
    # LOGIC & DATA HANDLING
    # ============================================
    def handle_data(self, data):
        cmd = data.get('cmd')
        if cmd == 'STATUS':
            self.time_lbl.config(text=f"{data.get('time', '--')} {data.get('ampm', '')}")
            self.next_lbl.config(text=data.get('nextAlarm', 'None'))
            st = data.get('state', 0)
            self.state_lbl.config(
                text={0: "Idle", 1: "Alarm", 2: "Dispense", 3: "Monitor", 4: "Taken", 5: "Missed"}.get(st, str(st)))
            self.missed_lbl.config(text=str(data.get('missedCount', 0)))
            if st == 5:
                self.alert_banner.config(bg=COLORS['danger'], text="⚠ MISSED PILL")
            elif st in [1, 2, 3]:
                self.alert_banner.config(bg=COLORS['warning'], text=f"🔔 DISPENSING: {data.get('currentTablet')}")
            else:
                self.alert_banner.config(bg=COLORS['success'], text="✓ SYSTEM READY")

        elif cmd == 'GET_ALARMS':
            self.alarms = {a['id']: a for a in data.get('alarms', [])}
            self.update_alarm_ui()
            self.update_schedule_ui()  # Updates logic for stock availability display

        elif cmd == 'GET_TABLETS':
            global TABLET_NAMES
            TABLET_NAMES = data.get('names', [])
            self.alarm_med_combo.config(values=TABLET_NAMES)
            self.update_tablet_list_ui()

        elif cmd == 'PILL_TAKEN':
            t_name = data.get('tablet')
            aid = data.get('alarm_id')
            self.log(f"Taken: {t_name}")
            self.add_history(t_name, "TAKEN")
            self.decrement_stock_and_check(t_name)
            if aid in self.alarms:
                self.alarms[aid]['status'] = 'TAKEN'
                self.update_schedule_ui()

        elif cmd == 'PILL_MISSED':
            t_name = data.get('tablet')
            aid = data.get('alarm_id')
            self.log(f"Missed: {t_name}", "ERROR")
            self.add_history(t_name, "MISSED")
            if aid in self.alarms:
                self.alarms[aid]['status'] = 'MISSED'
                self.update_schedule_ui()
            self.send_missed_alert(t_name)

    # --- KEY LOGIC: STOCK & EMAIL ---
    def decrement_stock_and_check(self, t_name):
        """Decrements stock. If it hits 0, it sends the email."""
        if t_name in TABLET_DETAILS:
            curr = TABLET_DETAILS[t_name].get('stock', 0)
            if curr > 0:
                TABLET_DETAILS[t_name]['stock'] -= 1
                save_tablet_data()
                self.update_tablet_list_ui()
                self.update_schedule_ui()  # Update UI to show "UNAVAILABLE" if now 0

                # Check if it JUST reached 0
                if TABLET_DETAILS[t_name]['stock'] == 0:
                    self.send_out_of_stock_alert(t_name)

    def send_out_of_stock_alert(self, t_name):
        self.log(f"⚠ OUT OF STOCK: {t_name}", "WARNING")
        if SERIAL_CONNECTED:
            send_command(f"LCD:OUT_OF_STOCK:{t_name}")

        subject = f"⚠ URGENT: {t_name} Stock Depleted"
        body = f"""
        <div class="alert-box" style="border-left-color: #E74C3C; background-color: #FDEDEC;">
            <h3>🛑 MEDICATION UNAVAILABLE</h3>
            <p>The inventory for <strong>{t_name}</strong> has reached <strong>0 units</strong>.</p>
            <p>Future alarms for this medication will be marked as <strong>UNAVAILABLE</strong> and cannot be dispensed.</p>
            <p><strong>Action Required:</strong> Please refill the dispenser immediately.</p>
        </div>
        """
        send_email_notification(subject, body)

    def send_missed_alert(self, t_name):
        subject = f"❌ MISSED DOSE: {t_name}"
        body = f"<div class='alert-box'><h3>MISSED DOSE</h3><p>Patient failed to take <strong>{t_name}</strong>.</p></div>"
        send_email_notification(subject, body)

    # --- UI UPDATERS ---
    def update_schedule_ui(self):
        """Updates the schedule list. Checks stock to show PENDING or UNAVAILABLE."""
        for w in self.sched_frame.winfo_children():
            w.destroy()

        if not self.alarms:
            tk.Label(self.sched_frame, text="No Schedule", bg='white').pack(pady=20)
            return

        for aid, a in sorted(self.alarms.items(), key=lambda x: (x[1]['h'], x[1]['m'])):
            f = tk.Frame(self.sched_frame, bg=COLORS['light'], relief='solid', borderwidth=1)
            f.pack(fill='x', pady=2)

            h12, ap = convert_to_12hr(a['h'])
            t_raw = a.get('t')
            t_name = TABLET_NAMES[t_raw] if isinstance(t_raw, int) and t_raw < len(TABLET_NAMES) else str(t_raw)

            # --- CHECK STOCK AVAILABILITY ---
            stock_count = TABLET_DETAILS.get(t_name, {}).get('stock', 0)
            status_text = a.get('status', 'PENDING')
            status_color = COLORS['text_light']

            if status_text == 'PENDING':
                if stock_count > 0:
                    status_text = "PENDING"
                    status_color = COLORS['accent']  # Blue
                else:
                    status_text = "UNAVAILABLE"
                    status_color = COLORS['danger']  # Red
            elif 'TAKEN' in status_text:
                status_color = COLORS['success']
            elif 'MISSED' in status_text:
                status_color = COLORS['danger']

            # Render
            tk.Label(f, text=f"{h12:02}:{a['m']:02} {ap}", font=('bold'), bg=COLORS['secondary'], fg='white',
                     width=10).pack(side='left', fill='y')
            tk.Label(f, text=f" {t_name}", bg=COLORS['light'], font=('Segoe UI', 11)).pack(side='left')
            tk.Label(f, text=status_text, fg=status_color, bg=COLORS['light'], font=('bold')).pack(side='right',
                                                                                                   padx=10)

    def update_alarm_ui(self):
        for w in self.alarm_list_frame.winfo_children():
            w.destroy()
        for aid, a in sorted(self.alarms.items(), key=lambda x: (x[1]['h'], x[1]['m'])):
            f = tk.Frame(self.alarm_list_frame, bg='white', relief='solid', borderwidth=1)
            f.pack(fill='x', pady=2)
            t_raw = a.get('t')
            t_name = TABLET_NAMES[t_raw] if isinstance(t_raw, int) and t_raw < len(TABLET_NAMES) else str(t_raw)
            tk.Label(f, text=f"{a['h']:02}:{a['m']:02}", font=('bold'), bg=COLORS['accent'], fg='white').pack(
                side='left', padx=5)
            tk.Label(f, text=t_name, bg='white').pack(side='left', padx=5)
            ttk.Button(f, text="Del", style='Danger.TButton', command=lambda i=aid: self.del_alarm(i)).pack(
                side='right')

    def update_tablet_list_ui(self):
        for i in self.tab_tree.get_children():
            self.tab_tree.delete(i)
        for name in TABLET_NAMES:
            det = TABLET_DETAILS.get(name, {'dosage': '-', 'stock': 0})
            st = det['stock']
            tag = 'low' if st < 5 else 'ok'
            if st == 0:
                tag = 'empty'
            self.tab_tree.insert('', 'end', values=(name, det['dosage'], st), tags=(tag,))
        self.tab_tree.tag_configure('low', foreground='orange')
        self.tab_tree.tag_configure('empty', foreground='red')

    # ============================================
    # ACTIONS
    # ============================================
    def add_alarm(self):
        try:
            h = int(self.hour_var.get())
            m = int(self.min_var.get())
            t_name = self.alarm_med_var.get()

            if not t_name:
                messagebox.showerror("Error", "Please select a medication.")
                return

            # --- 1. CHECK AVAILABILITY (BLOCK IF 0) ---
            current_stock = TABLET_DETAILS.get(t_name, {}).get('stock', 0)
            if current_stock == 0:
                messagebox.showerror("Unavailable", f"Cannot add alarm for {t_name}.\nStock is 0. Please refill.")
                return

            # --- 2. CHECK 2 MINUTE TIME GAP ---
            new_time_mins = h * 60 + m
            for aid, a in self.alarms.items():
                existing_mins = a['h'] * 60 + a['m']
                diff = abs(new_time_mins - existing_mins)
                # Basic check (does not handle midnight crossover for simplicity)
                if diff < 2:
                    t_existing = TABLET_NAMES[a['t']] if isinstance(a['t'], int) else str(a['t'])
                    messagebox.showerror("Time Conflict",
                                         f"⚠ Minimum 2-minute gap required.\n\nConflict with: {a['h']:02}:{a['m']:02} ({t_existing})")
                    return

            # --- 3. PROCEED ---
            if self.connection_status == "connected":
                try:
                    t_idx = TABLET_NAMES.index(t_name)
                    send_command(f"ADD_ALARM:{h}:{m}:{t_idx}")
                    self.log(f"Sent: {h}:{m} {t_name}")
                    self.master.after(500, lambda: send_command("GET_ALARMS"))
                except Exception:
                    messagebox.showerror("Error", "Sync Error.")
            else:
                # Simulation mode
                new_id = len(self.alarms) + 100
                self.alarms[new_id] = {'id': new_id, 'h': h, 'm': m, 't': t_name, 'status': 'PENDING'}
                self.update_schedule_ui()
                self.update_alarm_ui()

        except ValueError:
            messagebox.showerror("Error", "Invalid Time Input")

    def del_alarm(self, aid):
        if self.connection_status == "connected":
            send_command(f"DEL_ALARM:{aid}")
            self.master.after(500, lambda: send_command("GET_ALARMS"))
        else:
            if aid in self.alarms:
                del self.alarms[aid]
            self.update_schedule_ui()
            self.update_alarm_ui()

    def save_tablet(self):
        name = self.tab_name.get()
        if name:
            if name not in TABLET_NAMES:
                TABLET_NAMES.append(name)
            stock_val = int(self.tab_stock.get())
            TABLET_DETAILS[name] = {'dosage': '10mg', 'stock': stock_val}
            save_tablet_data()
            self.update_tablet_list_ui()
            self.update_schedule_ui()  # Refresh availability statuses
            self.alarm_med_combo.config(values=TABLET_NAMES)
            self.log(f"Inventory Saved: {name}")

    def refill_tablet(self):
        sel = self.tab_tree.selection()
        if sel:
            name = self.tab_tree.item(sel[0])['values'][0]
            if name in TABLET_DETAILS:
                TABLET_DETAILS[name]['stock'] += 10
                save_tablet_data()
                self.update_tablet_list_ui()
                self.update_schedule_ui()  # Should switch from UNAVAILABLE to PENDING

    def delete_tablet(self):
        sel = self.tab_tree.selection()
        if sel:
            name = self.tab_tree.item(sel[0])['values'][0]
            TABLET_NAMES.remove(name)
            del TABLET_DETAILS[name]
            save_tablet_data()
            self.update_tablet_list_ui()

    def save_email(self):
        EMAIL_CONFIG['recipient_primary'] = self.email1.get()
        messagebox.showinfo("Saved", "Email Saved")

    def add_history(self, med, status):
        t = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        ALARM_HISTORY.append({'time': t, 'med': med, 'status': status})
        self.hist_tree.insert('', 0, values=(t, med, status))

    def export_csv(self):
        try:
            with open("history.csv", "w", newline='') as f:
                w = csv.writer(f)
                w.writerow(["Time", "Medication", "Status"])
                for h in ALARM_HISTORY:
                    w.writerow([h['time'], h['med'], h['status']])
            messagebox.showinfo("Done", "Exported")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def log(self, msg, level="INFO"):
        self.log_text.insert('end', f"[{datetime.now().strftime('%H:%M:%S')}] {msg}\n")
        self.log_text.see('end')

    def update_connection_indicator(self):
        txt = "● CONNECTED" if SERIAL_CONNECTED else "● OFFLINE"
        col = COLORS['success'] if SERIAL_CONNECTED else COLORS['warning']
        self.connection_indicator.config(text=txt, fg=col)

    def periodic_status_request(self):
        while True:
            if SERIAL_CONNECTED:
                send_command("STATUS")
            time.sleep(2)


if __name__ == '__main__':
    root = tk.Tk()
    app = PillDispenserGUI(root)

    def on_closing():
        if ser and ser.is_open:
            ser.close()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()
