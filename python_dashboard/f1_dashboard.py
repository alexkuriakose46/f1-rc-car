import pygame
import socket
import serial
import math
import time

# --- CONFIGURATION ---
USE_SERIAL = False 
SERIAL_PORT = "COM6"
BAUD_RATE = 115200

UDP_IP = "192.168.4.1"
UDP_PORT = 4210

# --- VEHICLE DYNAMICS ---
class F1Vehicle:
    def __init__(self):
        self.gear = 0 # -1 = Reverse, 0 = Neutral, 1-6 = Forward
        self.hinge_val = 0
        self.light_mode = 0 # 0 = Off, 1 = F1 Blink Mode
        self.color_idx = 0
        
        self.throttle_input = 0.0
        self.brake_input = 0.0
        self.steering_input = 0.0
        
        self.motor_output = 0.0
        
    def update(self):
        if self.gear == 0:
            self.motor_output = 0
        elif self.gear == -1:
            self.motor_output = -self.throttle_input * 100
        else:
            self.motor_output = self.throttle_input * 100
            
        if self.brake_input > 0.05:
            self.motor_output = 0

# --- DASHBOARD UI WIDGETS ---
def draw_modern_gauge(surface, x, y, radius, value, max_val, title, glow_color, tick_step, unit_text):
    pygame.draw.circle(surface, (15, 15, 18), (x, y), radius)
    pygame.draw.circle(surface, (40, 40, 45), (x, y), radius, 4)
    
    min_angle = math.radians(225)
    max_angle = math.radians(-45)
    
    font_num = pygame.font.SysFont("Arial", 20, bold=True)
    for i in range(0, max_val + 1):
        pct = i / max_val
        ang = min_angle - (pct * (min_angle - max_angle))
        is_major = (i % tick_step == 0)
        inner_r = radius - (20 if is_major else 10)
        outer_r = radius - 4
        
        p1 = (x + math.cos(ang) * inner_r, y - math.sin(ang) * inner_r)
        p2 = (x + math.cos(ang) * outer_r, y - math.sin(ang) * outer_r)
        pygame.draw.line(surface, (150, 150, 150), p1, p2, 3 if is_major else 1)
        
        if is_major:
            text_r = radius - 40
            tx = x + math.cos(ang) * text_r
            ty = y - math.sin(ang) * text_r
            txt_surf = font_num.render(str(i), True, (220, 220, 220))
            surface.blit(txt_surf, (tx - txt_surf.get_width()//2, ty - txt_surf.get_height()//2))

    val_clamped = max(0, min(value, max_val))
    pct = val_clamped / max_val
    current_angle = min_angle - (pct * (min_angle - max_angle))
    
    glow_steps = int(pct * 50)
    for s in range(glow_steps):
        a1 = min_angle - ((s/50.0) * (min_angle - max_angle))
        a2 = min_angle - (((s+1)/50.0) * (min_angle - max_angle))
        p1 = (x + math.cos(a1)*(radius-10), y - math.sin(a1)*(radius-10))
        p2 = (x + math.cos(a2)*(radius-10), y - math.sin(a2)*(radius-10))
        pygame.draw.line(surface, glow_color, p1, p2, 8)
        
    needle_len = radius - 25
    nx = x + math.cos(current_angle) * needle_len
    ny = y - math.sin(current_angle) * needle_len
    pygame.draw.line(surface, glow_color, (x, y), (nx, ny), 5)
    
    pygame.draw.circle(surface, (100, 100, 100), (x, y), 14)
    pygame.draw.circle(surface, (200, 200, 200), (x, y), 10)
    pygame.draw.circle(surface, (50, 50, 50), (x, y), 4)
    
    title_font = pygame.font.SysFont("Arial", 18, bold=True)
    title_surf = title_font.render(title, True, glow_color)
    surface.blit(title_surf, (x - title_surf.get_width()//2, y - radius - 35))
    
    unit_font = pygame.font.SysFont("Arial", 14)
    unit_surf = unit_font.render(unit_text, True, (100, 100, 100))
    surface.blit(unit_surf, (x - unit_surf.get_width()//2, y + radius - 45))
    
    dig_font = pygame.font.SysFont("Courier", 36, bold=True)
    dig_surf = dig_font.render(str(int(val_clamped)), True, glow_color)
    surface.blit(dig_surf, (x - dig_surf.get_width()//2, y + radius - 85))

def draw_modern_pedal(surface, x, y, width, height, value, color, label):
    pygame.draw.rect(surface, (50, 50, 50), (x, y, width, height), border_radius=6, width=2)
    pygame.draw.rect(surface, (20, 20, 20), (x+2, y+2, width-4, height-4), border_radius=5)
    fill_h = value * (height-4)
    if fill_h > 0:
        pygame.draw.rect(surface, color, (x+2, y+height-2-fill_h, width-4, fill_h), border_radius=5)
    font = pygame.font.SysFont("Arial", 16, bold=True)
    txt = font.render(label, True, (150, 150, 150))
    surface.blit(txt, (x + width//2 - txt.get_width()//2, y + height + 10))

def draw_steering_slider(surface, x, y, width, value):
    pygame.draw.line(surface, (60, 60, 60), (x, y), (x + width, y), 6)
    for i in range(11):
        tx = x + (i/10.0) * width
        pygame.draw.line(surface, (100, 100, 100), (tx, y-8), (tx, y+8), 2)
    kx = x + (width/2) + (value * (width/2))
    pygame.draw.circle(surface, (200, 200, 200), (int(kx), y), 15)
    pygame.draw.circle(surface, (255, 255, 255), (int(kx), y), 10)
    font = pygame.font.SysFont("Arial", 22, bold=True, italic=True)
    txt = font.render("STEERING", True, (200, 200, 200))
    surface.blit(txt, (x + width//2 - txt.get_width()//2, y + 25))

# --- MAIN APP ---
def main():
    pygame.init()
    pygame.joystick.init()
    
    width, height = 900, 550
    screen = pygame.display.set_mode((width, height))
    pygame.display.set_caption("F1 RC Telemetry Dashboard")
    clock = pygame.time.Clock()
    
    vehicle = F1Vehicle()
    
    sock = None
    ser = None
    if USE_SERIAL:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0)
            print(f"Connected to {SERIAL_PORT} for WIRED testing.")
        except Exception as e:
            print(f"Could not open {SERIAL_PORT}. Ensure the Arduino IDE Serial Monitor is closed!")
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"Using Wi-Fi UDP on {UDP_IP}:{UDP_PORT}")
        
    packets_sent = 0
    joystick = None
    if pygame.joystick.get_count() > 0:
        joystick = pygame.joystick.Joystick(0)
        joystick.init()

    font_large = pygame.font.SysFont("Arial", 56, bold=True, italic=True)
    font_status = pygame.font.SysFont("Arial", 18)
    
    running = True
    prev_dx = 0
    
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                
            if event.type == pygame.JOYDEVICEADDED:
                if joystick is None:
                    joystick = pygame.joystick.Joystick(event.device_index)
                    joystick.init()
                    
            if event.type == pygame.JOYDEVICEREMOVED:
                if joystick:
                    joystick.quit()
                    joystick = None
                
            if event.type == pygame.JOYBUTTONDOWN:
                if event.button == 4: # LB (Gear Down)
                    vehicle.gear = max(-1, vehicle.gear - 1)
                elif event.button == 5: # RB (Gear Up)
                    vehicle.gear = min(6, vehicle.gear + 1)
                elif event.button == 2: # X Button (Quick Neutral)
                    vehicle.gear = 0
                elif event.button == 3: # Y Button (Toggle Light)
                    vehicle.light_mode = 1 if vehicle.light_mode == 0 else 0

        if joystick:
            axis_steer = joystick.get_axis(0) 
            try:
                axis_lt = joystick.get_axis(4)    
                axis_rt = joystick.get_axis(5)    
            except:
                axis_lt = -1.0
                axis_rt = -1.0
            
            lt_val = (axis_lt + 1.0) / 2.0
            rt_val = (axis_rt + 1.0) / 2.0
            
            if abs(axis_steer) < 0.1: axis_steer = 0
            if lt_val < 0.05: lt_val = 0
            if rt_val < 0.05: rt_val = 0
            
            vehicle.steering_input = axis_steer
            vehicle.brake_input = lt_val
            vehicle.throttle_input = rt_val
            
            if joystick.get_numhats() > 0:
                dx, dy = joystick.get_hat(0)
                vehicle.hinge_val = dy # Up is 1, Down is -1
                
                # D-pad left/right to cycle colors (0-5)
                if dx == 1 and prev_dx != 1:
                    vehicle.color_idx = (vehicle.color_idx + 1) % 6
                elif dx == -1 and prev_dx != -1:
                    vehicle.color_idx = (vehicle.color_idx - 1) % 6
                prev_dx = dx

        vehicle.update()
        
        try:
            # Scale outputs
            t_val = int(vehicle.throttle_input * 100)
            b_val = int(vehicle.brake_input * 100)
            s_val = int(vehicle.steering_input * 100)
            
            # Formatted exactly as ESP32 expects: G:1;T:100;B:0;S:90;H:0;L:1;C:0
            cmd = f"G:{vehicle.gear};T:{t_val};B:{b_val};S:{s_val};H:{vehicle.hinge_val};L:{vehicle.light_mode};C:{vehicle.color_idx}\n"
            
            if USE_SERIAL and ser and ser.is_open:
                ser.write(cmd.encode('utf-8'))
                packets_sent += 1
            elif not USE_SERIAL and sock:
                sock.sendto(cmd.encode('utf-8'), (UDP_IP, UDP_PORT))
                packets_sent += 1
        except Exception:
            pass

        # --- RENDERING ---
        screen.fill((5, 5, 8))
        
        pygame.draw.circle(screen, (0, 255, 0) if joystick else (255, 0, 0), (25, 25), 6)
        stat_txt = font_status.render(f"Status: {'Controller Connected' if joystick else 'No Controller'}   |   Packets: {packets_sent}", True, (200, 200, 200))
        screen.blit(stat_txt, (45, 15))
        
        speed_kmh = (abs(vehicle.motor_output) / 100.0) * 25.0
        draw_modern_gauge(screen, 230, 260, 160, speed_kmh, 25, "SPEED (Output %)", (0, 150, 255), 5, "km/h")
        
        rpm_val = vehicle.throttle_input * 8.0
        draw_modern_gauge(screen, 670, 260, 160, rpm_val, 8, "RPM (Throttle %)", (255, 20, 20), 1, "x1000")
        
        if vehicle.gear == 0:
            mode_text = "N"
            mode_color = (150, 150, 150)
        elif vehicle.gear == -1:
            mode_text = "R"
            mode_color = (255, 200, 0)
        else:
            mode_text = f"{vehicle.gear}"
            mode_color = (0, 200, 255)
            
        txt_mode = font_large.render(mode_text, True, mode_color)
        screen.blit(txt_mode, (450 - txt_mode.get_width()//2, 130))
        
        if vehicle.light_mode == 1:
            light_txt = font_status.render("F1 LIGHT: ON", True, (255, 50, 50))
        else:
            light_txt = font_status.render("F1 LIGHT: OFF", True, (100, 100, 100))
        screen.blit(light_txt, (450 - light_txt.get_width()//2, 190))
        
        color_names = ["OFF", "WHITE", "RED", "GREEN", "BLUE", "YELLOW"]
        color_rgb = [(100,100,100), (255,255,255), (255,50,50), (50,255,50), (50,150,255), (255,255,50)]
        c_name = color_names[vehicle.color_idx]
        c_rgb = color_rgb[vehicle.color_idx]
        hl_txt = font_status.render(f"HEADLIGHT: {c_name}", True, c_rgb)
        screen.blit(hl_txt, (450 - hl_txt.get_width()//2, 210))
        
        draw_modern_pedal(screen, 400, 250, 35, 90, vehicle.brake_input, (100, 100, 100), "BRK")
        draw_modern_pedal(screen, 465, 250, 35, 90, vehicle.throttle_input, (255, 20, 20), "THR")
        draw_steering_slider(screen, 250, 500, 400, vehicle.steering_input)

        pygame.display.flip()
        clock.tick(20)

    pygame.quit()

if __name__ == "__main__":
    main()
