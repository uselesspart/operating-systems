"""Сквозная проверка отдельных исполняемых файлов и правил игры."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


JOIN = re.compile(r'\[INFO\] Client (\d+) \(PID (\d+)\) connected')
LEAVE = re.compile(r'\[INFO\] Client (\d+) \(PID (\d+)\) disconnected')
START = re.compile(r'\[INFO\] Round (\d+) started; enter wolf number within 3 seconds')
RESULT = re.compile(
    r'\[INFO\] Round (\d+): wolf = (\d+), hidden = (\d+), '
    r'caught = (\d+), revived = (\d+), dead = (\d+)')
MOVE = re.compile(r'\s+Client (\d+): (\d+) - (Hidden|Caught|Revived|Remained dead)')
CLIENT_MOVE = re.compile(r'\[INFO\] Round (\d+): number (\d+)')
CLIENT_STATE = re.compile(r'\[INFO\] State: (Alive|Dead)')


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_for(predicate, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.025)
    raise AssertionError(description)


def verify_rounds(text):
    # Восстанавливаем состав раунда по событиям, а результат вычисляем независимо от C++.
    states = {}
    participants = set()
    initial_count = 0
    active_round = None
    streak = 0
    game_over = False
    moves = {}
    lines = text.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        index += 1
        if match := JOIN.fullmatch(line):
            client_id = int(match[1])
            require(client_id not in states, 'Duplicate client ID')
            states[client_id] = True
        elif match := LEAVE.fullmatch(line):
            client_id = int(match[1])
            require(client_id in states, 'Unknown disconnected client')
            del states[client_id]
            participants.discard(client_id)
            if not states and not game_over:
                streak = 0
        elif match := START.fullmatch(line):
            require(states and active_round is None and not game_over,
                    'Unexpected round start')
            active_round = int(match[1])
            participants = set(states)
            initial_count = len(participants)
        elif line == '[INFO] Round cancelled: no remaining participants':
            require(active_round is not None and not participants, 'Unexpected cancellation')
            active_round = None
        elif match := RESULT.fullmatch(line):
            round_id, wolf, hidden, caught, revived, dead = map(int, match.groups())
            require(active_round == round_id and participants, 'Unexpected completed round')
            require(1 <= wolf <= 100, 'Wolf number out of range')
            observed = set()
            counts = {'Hidden': 0, 'Caught': 0, 'Revived': 0, 'Remained dead': 0}
            while index < len(lines) and (move := MOVE.fullmatch(lines[index])):
                index += 1
                client_id, number = int(move[1]), int(move[2])
                require(client_id in participants and client_id not in observed,
                        'Unexpected participant or duplicate move')
                observed.add(client_id)
                alive = states[client_id]
                require(1 <= number <= (100 if alive else 50), 'Kid number out of range')
                difference = abs(wolf - number)
                if alive:
                    outcome = 'Hidden' if difference * initial_count <= 70 else 'Caught'
                else:
                    outcome = 'Revived' if difference * initial_count <= 20 else 'Remained dead'
                require(move[3] == outcome, f'Round {round_id}: wrong outcome for client {client_id}')
                alive = outcome in ('Hidden', 'Revived')
                states[client_id] = alive
                counts[outcome] += 1
                moves[client_id, round_id] = (number, alive)
            require(observed == participants, 'Missing participant results')
            require((hidden, caught, revived, dead) == (
                counts['Hidden'], counts['Caught'], counts['Revived'],
                counts['Caught'] + counts['Remained dead']), 'Incorrect round statistics')
            streak = streak + 1 if dead == len(participants) else 0
            game_over = streak >= 2
            active_round = None
            participants.clear()
    require(moves, 'No completed moves to validate')
    require(game_over == ('[INFO] Game over' in lines), 'Incorrect game-over condition')
    return moves


def verify_client(text, moves):
    match = re.search(r'\[INFO\] Connected as client (\d+)', text)
    require(match is not None, 'Missing assigned client ID')
    client_id = int(match[1])
    alive = True
    current = None
    results = 0
    for line in text.splitlines():
        if move := CLIENT_MOVE.fullmatch(line):
            require(current is None, 'Client started another round before receiving its state')
            round_id, number = int(move[1]), int(move[2])
            require(1 <= number <= (100 if alive else 50), 'Invalid client number')
            current = (round_id, number)
        elif state := CLIENT_STATE.fullmatch(line):
            require(current is not None, 'Unexpected client state')
            round_id, number = current
            alive = state[1] == 'Alive'
            require(moves.get((client_id, round_id)) == (number, alive),
                    'Client move/state differs from host result')
            current = None
            results += 1
    require(results > 0, 'Client received no round results')


def run(host_path, client_path, transport):
    processes = []
    logs = {}
    with tempfile.TemporaryDirectory(prefix='lab2-e2e-') as folder:
        def start(name, args):
            path = Path(folder) / (name + '.log')
            logs[name] = path
            with path.open('wb') as output:
                proc = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT,
                                        env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen'})
            processes.append(proc)
            return proc

        def log(name):
            return logs[name].read_text(errors='replace')

        def resources(pid):
            prefix = f'lab2-{transport}-{os.geteuid()}-{pid}-'
            return (list(Path('/tmp').glob(prefix + '*'))
                    + list(Path('/dev/shm').glob('sem.' + prefix + '*')))

        host = None
        try:
            invalid = start('invalid', [client_path, 'invalid'])
            require(invalid.wait(timeout=5) != 0, 'Invalid PID accepted')
            require('invalid host PID' in log('invalid'), 'Missing invalid PID diagnostic')
            host = start('host', [host_path])
            wait_for(lambda: 'Waiting for clients' in log('host'), 8, 'Host not ready')
            time.sleep(0.25)
            require(' started; enter wolf number' not in log('host'), 'Round started without clients')
            clients = [start(f'client{i}', [client_path, str(host.pid)]) for i in range(1, 4)]
            wait_for(lambda: sum(JOIN.fullmatch(line) is not None
                                 for line in log('host').splitlines()) == 3,
                     8, 'Three clients did not connect')

            # Убиваем одного клиента и проверяем результаты оставшихся после его отключения.
            victim = clients[0]
            require(victim.poll() is None, 'Client exited before disconnect test')
            victim.kill()
            victim.wait(timeout=5)
            disconnect = f'(PID {victim.pid}) disconnected'
            wait_for(lambda: disconnect in log('host'), 10, 'Killed client not removed')
            wait_for(lambda: any(RESULT.fullmatch(line) for line in
                                 log('host').split(disconnect, 1)[1].splitlines()),
                     12, 'No completed round after client failure')
            wait_for(lambda: all('[INFO] State: ' in log(name) for name in ('client2', 'client3')),
                     8, 'Healthy clients received no states')

            host.send_signal(signal.SIGTERM)
            require(host.wait(timeout=12) == 0, 'Host did not stop normally')
            for proc in clients[1:]:
                require(proc.wait(timeout=8) == 0, 'Healthy client did not exit normally')
            host_log = log('host')
            require('[INFO] Host stopped' in host_log, 'Missing shutdown event')
            require('Host failed:' not in host_log, 'Host controller failed')
            moves = verify_rounds(host_log)
            for name in ('client2', 'client3'):
                verify_client(log(name), moves)
            require(not resources(host.pid), f'IPC resources remain: {resources(host.pid)}')
            print(f'{transport}: independent clients, game rules, disconnect, shutdown and cleanup passed')
        except BaseException:
            for name in logs:
                print(f'--- {name} ---\n{log(name)}', file=sys.stderr)
            raise
        finally:
            # Даже при провале проверки сначала даём хосту возможность удалить ресурсы.
            if host is not None and host.poll() is None:
                host.send_signal(signal.SIGTERM)
                try:
                    host.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    pass
            for proc in reversed(processes):
                if proc.poll() is None:
                    proc.kill()
                proc.wait(timeout=5)


if __name__ == '__main__':
    run(*sys.argv[1:])
