import asyncio
import csv
import json
import struct
import sys
import time
from collections import deque
from pathlib import Path

import numpy as np
import pyqtgraph as pg
from bleak import BleakClient, BleakScanner
from PyQt5 import QtCore, QtWidgets

ROOT = Path(__file__).parent
CSV = ROOT / "data_log.csv"
CFG = json.loads((ROOT / "config.json").read_text())


class Ble(QtCore.QThread):
    accel = QtCore.pyqtSignal(object, float, float, float)
    temp = QtCore.pyqtSignal(object, float)
    phone = QtCore.pyqtSignal(object, str)
    status = QtCore.pyqtSignal(str, str)

    def __init__(self):
        super().__init__()
        self.stop = False

    def run(self):
        asyncio.run(self.main())

    async def main(self):
        await asyncio.gather(self.loop("esp32"), self.loop("phone"))

    async def loop(self, kind):
        c = CFG[kind]
        while not self.stop:
            try:
                self.status.emit(kind, "buscando")
                print(f"Buscando dispositivo por nombre: {c['name']}")
                dev = await BleakScanner.find_device_by_name(c["name"], timeout=5.0)
                if not dev:
                    print(f"No encontrado por nombre: {c['name']}")
                    await asyncio.sleep(2)
                    continue
                print(f"Encontrado por nombre: name={dev.name}, address={dev.address}")
                self.status.emit(kind, "conectando")
                async with BleakClient(dev) as cli:
                    self.status.emit(kind, "conectado")
                    self.print_services(cli)
                    if kind == "esp32":
                        if c.get("accel_char_uuid"):
                            await cli.start_notify(c["accel_char_uuid"], self.on_accel)
                        if c.get("temp_char_uuid"):
                            await cli.start_notify(c["temp_char_uuid"], self.on_temp)
                    else:
                        try:
                            await cli.start_notify(c["char_uuid"], self.on_phone)
                        except Exception:
                            pass
                    while cli.is_connected and not self.stop:
                        if kind == "phone":
                            try:
                                self.emit_phone(await cli.read_gatt_char(c["char_uuid"]))
                            except Exception:
                                pass
                        await asyncio.sleep(1)
            except Exception as e:
                self.status.emit(kind, f"reconectando: {e}")
                await asyncio.sleep(2)

    def print_services(self, cli):
        print(f"Conectado a {cli.name} con direccion {cli.address}")
        for service in cli.services:
            print(f"- Servicio {service.uuid}")
            for char in service.characteristics:
                print(f"  - Caracteristica {char.uuid}")

    def on_accel(self, _, data):
        if len(data) >= 16:
            self.accel.emit(*struct.unpack("<Ifff", data[:16]))

    def on_temp(self, _, data):
        if len(data) >= 8:
            self.temp.emit(*struct.unpack("<If", data[:8]))

    def on_phone(self, _, data):
        self.emit_phone(data)

    def emit_phone(self, data):
        try:
            text = bytes(data).decode().strip()
        except UnicodeDecodeError:
            text = bytes(data).hex()
        self.phone.emit(int(time.time() * 1000), text)


class App(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.data = deque(maxlen=6000)
        self.log = None
        self.win_ms = max(2000, CFG.get("ui", {}).get("window_ms", 2000))
        self.build_ui()
        self.ble = Ble()
        self.ble.accel.connect(self.on_accel)
        self.ble.temp.connect(self.on_temp)
        self.ble.phone.connect(self.on_phone)
        self.ble.status.connect(self.on_status)
        self.ble.start()
        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.redraw)
        self.timer.start(100)

    def build_ui(self):
        self.setWindowTitle("Tarea 1 IoT")
        self.resize(900, 600)
        w = QtWidgets.QWidget()
        self.setCentralWidget(w)
        v = QtWidgets.QVBoxLayout(w)
        row = QtWidgets.QHBoxLayout()
        v.addLayout(row)

        self.show_acc = QtWidgets.QCheckBox("Acelerometro")
        self.show_temp = QtWidgets.QCheckBox("Temperatura")
        self.show_phone = QtWidgets.QCheckBox("Telefono")
        self.btn = QtWidgets.QPushButton("Iniciar CSV")
        self.btn.clicked.connect(self.csv_toggle)
        for x in [self.show_acc, self.show_temp, self.show_phone]:
            x.setChecked(True)
        for x in [self.show_acc, self.show_temp, self.show_phone, self.btn]:
            row.addWidget(x)

        self.lbl = QtWidgets.QLabel("ESP32: -- | Telefono: --\nTemp: --\nPhone: --\nStats: --")
        v.addWidget(self.lbl)
        self.plot = pg.PlotWidget()
        v.addWidget(self.plot, 1)
        self.cx = self.plot.plot(pen="r")
        self.cy = self.plot.plot(pen="g")
        self.cz = self.plot.plot(pen="b")
        self.st = {"esp32":"--", "phone":"--", "temp":"--", "phone_val":"--", "stats":"--"}

    def label(self):
        temp = self.st["temp"] if self.show_temp.isChecked() else "oculto"
        phone = self.st["phone_val"] if self.show_phone.isChecked() else "oculto"
        self.lbl.setText(f"ESP32: {self.st['esp32']} | Telefono: {self.st['phone']}\nTemp: {temp}\nPhone: {phone}\nStats: {self.st['stats']}")

    def on_status(self, k, s):
        self.st[k] = s
        self.label()

    def on_accel(self, t, x, y, z):
        self.data.append((t, x, y, z))
        self.write([t, "esp32_accel", x, y, z, "", ""])

    def on_temp(self, t, val):
        self.st["temp"] = f"{val:.2f} C @ {t}"
        self.label()
        self.write([t, "esp32_temp", "", "", "", val, ""])

    def on_phone(self, t, val):
        self.st["phone_val"] = f"{val} @ {t}"
        self.label()
        self.write([t, "phone", "", "", "", "", val])

    def redraw(self):
        if not self.show_acc.isChecked() or not self.data:
            self.cx.setData([])
            self.cy.setData([])
            self.cz.setData([])
            return
        end = self.data[-1][0]
        a = np.array([r for r in self.data if end - r[0] <= self.win_ms], dtype=float)
        x = a[:, 0] - a[-1, 0]
        self.cx.setData(x, a[:, 1])
        self.cy.setData(x, a[:, 2])
        self.cz.setData(x, a[:, 3])
        s = np.array(list(self.data)[-1000:], dtype=float)[:, 1:4]
        self.st["stats"] = f"RMS {np.sqrt((s*s).mean(0)).round(2)} | Peak {s.max(0).round(2)} | P-P {np.ptp(s,0).round(2)}"
        self.label()

    def csv_toggle(self):
        if self.log:
            self.log.close()
            self.log = None
            self.btn.setText("Iniciar CSV")
            return
        new = not CSV.exists() or CSV.stat().st_size == 0
        self.log = CSV.open("a", newline="")
        self.writer = csv.writer(self.log)
        self.btn.setText("Detener CSV")
        if new:
            self.writer.writerow(["timestamp_ms", "source", "ax", "ay", "az", "temperatura", "valor_celular"])

    def write(self, row):
        if self.log:
            self.writer.writerow(row)
            self.log.flush()

    def closeEvent(self, e):
        self.ble.stop = True
        if not self.ble.wait(3000):
            self.ble.terminate()
        if self.log:
            self.log.close()
        super().closeEvent(e)


if __name__ == "__main__":
    q = QtWidgets.QApplication(sys.argv)
    a = App()
    a.show()
    sys.exit(q.exec_())
