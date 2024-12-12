import socket
import threading
import tkinter as tk
from tkinter import messagebox
import customtkinter as ctk
import time
import select
from constants import *

# Constants
BUFFER_SIZE = 1024

# Global Variables
client_socket = None
stop_event = threading.Event()


def _on_close():
    print("[DEBUG] Closing client...")
    stop_event.set()
    try:
        client_socket.shutdown(socket.SHUT_RDWR)
        client_socket.close()
        exit(0)
    except Exception:
        # print(f"[DEBUG] Error during shutdown: {e}")
        exit(0)
    finally:
        exit(0)


class GameClient:
    def __init__(self):
        self._on_close = _on_close
        self.server_ip = ""
        self.server_port = 0
        self.state = "connecting"  # Initial state
        self.chat_display = None
        self.send_button = None
        self.opponent_label = None

    def start(self):
        print("[DEBUG] Starting client...")
        self._setup_gui()

    def _setup_gui(self):
        ctk.set_appearance_mode("System")
        ctk.set_default_color_theme("blue")

        root = ctk.CTk()
        root.withdraw()

        # Prompt user for server IP and port
        self.server_ip = ctk.CTkInputDialog(text="Enter the server IP:", title="Server IP").get_input()
        server_port_str = ctk.CTkInputDialog(text="Enter the server port:", title="Server Port").get_input()

        if not self.server_ip or not server_port_str:
            messagebox.showerror("Error", "Server IP and Port are required!")
            print("[DEBUG] Missing IP or port input.")
            return

        try:
            self.server_port = int(server_port_str)
        except ValueError:
            messagebox.showerror("Error", "Invalid port number!")
            print("[DEBUG] Invalid port input.")
            return

        # Attempt to connect to the server
        try:
            global client_socket
            client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client_socket.connect((self.server_ip, self.server_port))

            # Wait for the SUCCESSFUL_CONNECTION message
            print("[DEBUG] Waiting for SUCCESSFUL_CONNECTION...")
            ready = select.select([client_socket], [], [], 5)  # 5-second timeout
            if ready[0]:
                initial_message = client_socket.recv(BUFFER_SIZE).decode('utf-8').strip()
                if initial_message != SUCCESSFUL_CONNECTION:
                    raise ValueError(f"Unexpected response from server: {initial_message}")
                else:
                    print("[DEBUG] SUCCESSFUL_CONNECTION received.")
            else:
                raise TimeoutError("No response from server within the timeout period.")

            messagebox.showinfo("Connected", f"Connected to server at {self.server_ip}:{self.server_port}")
            print(f"[DEBUG] Successfully connected to {self.server_ip}:{self.server_port}")

            # Open chat window
            self._open_chat_window(root)
        except Exception as e:
            messagebox.showerror("Connection Failed", f"Failed to connect to the correct server: {e}")
            print(f"[DEBUG] Connection failed: {e}")
            try:
                client_socket.close()
            except Exception:
                pass
            return

        root.mainloop()

    def _open_chat_window(self, root):
        print("[DEBUG] Opening chat window...")
        chat_window = ctk.CTkToplevel(root)
        chat_window.title("Game Chat")
        chat_window.geometry("600x500")

        # Nickname prompt and opponent connection status
        user_label = ctk.CTkLabel(chat_window, text="Enter your nickname and press 'Send'", anchor="w")
        user_label.pack(fill='x')

        self.opponent_label = ctk.CTkLabel(chat_window, text="Opponent (Disconnected)", anchor="e")
        self.opponent_label.pack(fill='x')

        # Scrollable text area for chat messages
        self.chat_display = ctk.CTkTextbox(chat_window, state='disabled', wrap=tk.WORD, height=20)
        self.chat_display.pack(pady=10, padx=10, fill='both', expand=True)

        # Entry field for sending messages
        message_entry = ctk.CTkEntry(chat_window)
        message_entry.pack(pady=5, padx=10, fill='x')

        self.send_button = ctk.CTkButton(
            chat_window,
            text="Send",
            command=lambda: self._send_message(message_entry)
        )
        self.send_button.pack(pady=5)
        self.send_button.configure()

        # Handle window close
        chat_window.protocol("WM_DELETE_WINDOW", self._on_close)
        ctk.CTkButton(chat_window, text="Exit", command=self._on_close).pack(pady=5)

        # Start background threads
        threading.Thread(target=self._keep_alive, daemon=True).start()
        threading.Thread(target=self._receive_messages, daemon=True).start()

    def _send_message(self, message_entry):
        message = message_entry.get()
        if message:
            try:
                if self.state == "in_game":  # Add prefix 'G' for game messages
                    message = f"G{message}"
                    print(f"[DEBUG] Sending game message: {message}")
                client_socket.send(message.encode('utf-8'))
                message_entry.delete(0, tk.END)
            except Exception as e:
                print(f"[DEBUG] Error sending message: {e}")
                self._reconnect_handler()

    def _receive_messages(self):
        buffer = ""
        while not stop_event.is_set():
            try:
                ready = select.select([client_socket], [], [], 1)
                if ready[0]:
                    data = client_socket.recv(BUFFER_SIZE).decode('utf-8')
                    if data:
                        print(f"[DEBUG] Raw received data: {data}")
                        buffer += data

                        while "\n" in buffer:
                            message, buffer = buffer.split("\n", 1)
                            self._handle_message(message.strip())
                    else:
                        self.opponent_label.configure(text="Opponent (Disconnected)")
                        print("[DEBUG] Received empty message. Connection may be closed by server.")
                        break
            except Exception as e:
                print(f"[DEBUG] Error receiving message: {e}")
                break

    def _handle_message(self, message):
        if message == SUCCESSFUL_CONNECTION:  # SC
            self.state = "waiting_for_nickname"
            self.send_button.configure(state='normal')  # Allow nickname entry
            self.opponent_label.configure(text="Opponent (Disconnected)")
            self._update_chat("Connected! Please enter your nickname.")
        elif message == NICKNAME_IN_USE:  # NIU
            self._update_chat("Nickname is already in use. Please try another.")
        elif message == NICKNAME_SET:  # NS
            self.state = "in_game"
            self._update_chat("Nickname set successfully. Waiting for opponent...")
            self.send_button.configure(state='disabled')  # Disable until the game starts
        elif message == UR_TURN:  # UT
            self.opponent_label.configure(text="Opponent (Connected)")
            self.state = "in_game"
            self._update_chat("It's your turn!")
            self.send_button.configure(state='normal')  # Enable message sending
            self.opponent_label.configure(text="Opponent (Connected)")
        elif message == OPP_TURN:  # OT
            self.opponent_label.configure(text="Opponent (Connected)")
            self.state = "in_game"
            self._update_chat("It's the opponent's turn.")
            self.send_button.configure(state='disabled')  # Disable message sending
            self.opponent_label.configure(text="Opponent (Connected)")
        elif message == OPPONENT_DISCONNECTED:  # OD
            self.state = "disconnected"
            self._update_chat("Your opponent has disconnected. Waiting for a new player.")
            self.send_button.configure(state='disabled')
            self.opponent_label.configure(text="Opponent (Disconnected)")
        elif message == GAME_START:  # GS
            self._update_chat("The game has started!")
            self.opponent_label.configure(text="Opponent (Connected)")
        elif message == WIN_MSG:  # WIN
            self._update_chat("Congratulations! You won!")
        elif message == LOST_MSG:  # LOST
            self._update_chat("You lost. Better luck next time!")
        elif message == ENDGAME_MSG:  # EG
            self._update_chat("Game over. Enter a new nickname to play again.")
            self.send_button.configure(state='normal')  # Allow new nickname entry
            self.opponent_label.configure(text="Opponent (Disconnected)")
        elif message == WRONG_FORMAT:  # WF
            self._update_chat("Invalid message format. Disconnecting...")
            stop_event.set()
            client_socket.close()
        elif message == INVALID_GUESS:  # WF
            self._update_chat("Invalid guess format. Try again...")
        elif message.startswith("G"):  # Guess response
            self._handle_guess_response(message)
        else:
            self._update_chat(f"Unknown message from server: {message}")

    def _handle_guess_response(self, message):
        # Message format: G<guess>B<bulls>C<cows>
        try:
            b_index = message.find("B")
            c_index = message.find("C")

            if b_index == -1 or c_index == -1 or b_index < 2 or c_index < b_index + 2:
                self._update_chat("[DEBUG] Malformed guess response from server.")
                return

            guess_part = message[1:b_index]
            bulls_part = message[b_index + 1:c_index]
            cows_part = message[c_index + 1:]

            self._update_chat(
                f"Guess: {guess_part} | Bulls: {bulls_part} | Cows: {cows_part}"
            )
        except Exception as e:
            print(f"[DEBUG] Error parsing guess response: {e}")
            self._update_chat("[DEBUG] Error parsing guess response from server.")

    def _update_chat(self, text):
        self.chat_display.configure(state='normal')
        self.chat_display.insert(tk.END, text + '\n')
        self.chat_display.configure(state='disabled')
        self.chat_display.yview(tk.END)

    def _keep_alive(self, interval=3):
        while not stop_event.is_set():
            try:
                client_socket.send("PING".encode('utf-8'))
                time.sleep(interval)
            except Exception as e:
                self.opponent_label.configure(text="Opponent (Disconnected)")
                print(f"[DEBUG] Keep-alive error: {e}")
                self._reconnect_handler()
                break

    def _reconnect_handler(self):
        stop_event.set()
        print("[DEBUG] Attempting to reconnect...")
        should_reconnect = messagebox.askyesno("Disconnected", "Reconnect to the server?")
        if should_reconnect:
            self._reconnect()
        else:
            _on_close()

    def _reconnect(self):
        global client_socket
        try:
            client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client_socket.connect((self.server_ip, self.server_port))
            print("[DEBUG] Reconnected successfully.")
            stop_event.clear()
            threading.Thread(target=self._keep_alive, daemon=True).start()
            threading.Thread(target=self._receive_messages, daemon=True).start()
        except Exception as e:
            messagebox.showerror("Reconnect Failed", "Unable to reconnect to the server.")
            print(f"[DEBUG] Reconnection failed: {e}")
            self._reconnect_handler()


if __name__ == "__main__":
    client = GameClient()
    client.start()
