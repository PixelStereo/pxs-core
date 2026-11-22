// SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-PixelStereo-Commercial
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:pxs_core/pxs_core.dart';
import 'package:test/test.dart';

String libraryPath() {
  final env = Platform.environment['PXS_C_LIBRARY'];
  if (env != null && env.isNotEmpty) return env;
  final name = Platform.isWindows ? 'pxs_c.dll' : Platform.isMacOS ? 'libpxs_c.dylib' : 'libpxs_c.so';
  return '../build-shared/$name';
}

void main() {
  setUpAll(() => Pxs.open(path: libraryPath()));

  test('the library is the version this package is written for', () {
    expect(Pxs.isOpen, isTrue);
    expect(Pxs.version, matches(RegExp(r'^\d+\.\d+\.\d+$')));
  });

  group('OSC', () {
    test('a message goes round', () {
      final bytes = Osc.encode('/mix/level', 'ifs', [7, 0.5, 'hé']);
      expect(bytes.length % 4, 0);
      final messages = Osc.decode(bytes);
      expect(messages, hasLength(1));
      expect(messages[0].address, '/mix/level');
      expect(messages[0].types, 'ifs');
      expect(messages[0].args, [7, 0.5, 'hé']);
    });

    test('every value kind is carried by its tag', () {
      final bytes = Osc.encode('/k', 'ihfdsbTFN', [1, 2, 0.25, 0.5, 'x', Uint8List.fromList([1, 2, 3]), true, false, null]);
      final args = Osc.decode(bytes).single.args;
      expect(args[0], 1);
      expect(args[1], 2);
      expect(args[2], 0.25);
      expect(args[3], 0.5);
      expect(args[4], 'x');
      expect(args[5], Uint8List.fromList([1, 2, 3]));
      expect(args[6], true);
      expect(args[7], false);
      expect(args[8], isNull);
    });

    test('a hostile packet is an exception, not a crash', () {
      expect(() => Osc.decode(Uint8List.fromList([1, 2, 3])), throwsA(isA<PxsException>().having((e) => e.status, 'status', -2)));
      expect(() => Osc.decode(Uint8List(0)), throwsA(isA<PxsException>()));
      final big = Uint8List(2 << 20)..[0] = 0x2f;
      expect(() => Osc.decode(big), throwsA(isA<PxsException>()));
    });
  });

  group('the address space', () {
    late PxsTree tree;
    var level = 0.25;
    var on = false;
    var mode = 'a';
    var cue = -1;
    var resets = 0;
    final told = <Uint8List>[];

    setUp(() {
      level = 0.25;
      on = false;
      mode = 'a';
      cue = -1;
      resets = 0;
      told.clear();
      tree = PxsTree();
      tree.onOutput = told.add;
      tree.addParameter('/mix/level', ParameterType.float, label: 'Level', get: () => level, set: (v) => level = (v as num).toDouble());
      tree.addParameter('/mix/on', ParameterType.bool_, get: () => on, set: (v) => on = v as bool);
      tree.addParameter('/osc/mode', ParameterType.choice, choices: ['a', 'b', 'c'], get: () => mode, set: (v) => mode = v as String);
      tree.addParameter('/osc/reset', ParameterType.trigger, set: (_) => resets++);
      tree.addAction('/show/go', 'i', (args) {
        cue = args.single as int;
        return true;
      });
      tree.addSignal('/detect/peak', 'f');
    });

    tearDown(() => tree.dispose());

    test('OSC writes a parameter, within its limits', () {
      expect(tree.applyPacket(Osc.encode('/mix/level', 'f', [9.0])), 1);
      expect(level, 1.0);
      expect(tree.applyPacket(Osc.encode('/mix/level', 's', ['loud'])), 0);
      expect(tree.applyPacket(Osc.encode('/mix/on', 'T', [])), 1);
      expect(on, isTrue);
      expect(tree.applyPacket(Osc.encode('/osc/mode', 's', ['B'])), 1);
      expect(mode, 'b');
      expect(tree.applyPacket(Osc.encode('/osc/reset', '', [])), 1);
      expect(resets, 1);
      expect(tree.applyPacket(Osc.encode('/show/go', 'i', [3])), 1);
      expect(cue, 3);
      expect(tree.applyPacket(Osc.encode('/mix/*', 'f', [0.5])), greaterThanOrEqualTo(1));
      expect(() => tree.applyPacket(Uint8List.fromList([1, 2, 3])), throwsA(isA<PxsException>()));
    });

    test('what the tree says comes out as OSC', () {
      level = 0.75;
      expect(tree.changed('/mix/level'), isTrue);
      expect(told, hasLength(1));
      final m = Osc.decode(told.single).single;
      expect(m.address, '/mix/level');
      expect(m.args, [0.75]);
      expect(tree.fire('/detect/peak', [0.9]), isTrue);
      expect(told, hasLength(2));
      expect(tree.fire('/mix/level', [1.0]), isFalse);
      expect(tree.changed('/nowhere'), isFalse);
    });

    test('an OSCQuery client is answered', () {
      final value = tree.oscQueryAnswer('GET', '/mix/level?VALUE');
      expect(value.status, 200);
      expect(jsonDecode(value.body!), {'VALUE': [0.25]});
      final host = tree.oscQueryAnswer('GET', '/?HOST_INFO', name: 'App', oscPort: 9000);
      expect(jsonDecode(host.body!)['OSC_PORT'], 9000);
      expect(tree.oscQueryAnswer('GET', '/nowhere').status, 404);
      expect(tree.oscQueryAnswer('GET', '/nowhere').body, isNull);
      expect(tree.oscQueryAnswer('POST', '/').status, 405);
      final root = jsonDecode(tree.oscQueryAnswer('GET', '/').body!) as Map<String, dynamic>;
      expect((root['CONTENTS'] as Map).keys, containsAll(['mix', 'osc', 'show', 'detect']));
    });

    test('presets and locks', () {
      final preset = tree.presetCapture('Intro');
      expect(jsonDecode(preset)['name'], 'Intro');
      level = 0.1;
      on = false;
      expect(tree.lock('/mix/on', true), isTrue);
      expect(tree.presetRecall(preset), greaterThanOrEqualTo(2));
      expect(level, 0.25);
      expect(on, isFalse); // the locked switch keeps its value
      expect(tree.lock('/nowhere', true), isFalse);
      expect(tree.presetRecall('not json'), 0);
    });

    test('a node can be removed', () {
      expect(tree.remove('/mix/on'), isTrue);
      expect(tree.remove('/mix/on'), isFalse);
      expect(tree.applyPacket(Osc.encode('/mix/on', 'T', [])), 0);
    });

    test('a disposed tree says so', () {
      final other = PxsTree()..dispose();
      other.dispose(); // twice is fine
      expect(() => other.applyPacket(Osc.encode('/a', '', [])), throwsStateError);
    });
  });

  test('a string given by a getter survives until the next read', () {
    final tree = PxsTree();
    var text = 'première';
    tree.addParameter('/t', ParameterType.text, get: () => text, set: (v) => text = v as String);
    expect(jsonDecode(tree.oscQueryAnswer('GET', '/t?VALUE').body!), {'VALUE': ['première']});
    text = 'seconde';
    expect(jsonDecode(tree.oscQueryAnswer('GET', '/t?VALUE').body!), {'VALUE': ['seconde']});
    tree.applyPacket(Osc.encode('/t', 's', ['écrit']));
    expect(text, 'écrit');
    tree.dispose();
  });

  group('DMX', () {
    test('an sACN frame goes round', () {
      final frame = DmxFrame(sacn: true, universe: 7, priority: 100, sequence: 9, name: 'Pixel Stereo', values: Uint8List.fromList(List.generate(512, (i) => i & 255)));
      final packet = Dmx.encode(frame);
      expect(packet.length, 638);
      final back = Dmx.decode(packet, sacn: true);
      expect(back.universe, 7);
      expect(back.priority, 100);
      expect(back.sequence, 9);
      expect(back.name, 'Pixel Stereo');
      expect(back.values[300], 300 & 255);
      expect(() => Dmx.decode(packet, sacn: false), throwsA(isA<PxsException>()));
    });

    test('an Art-Net frame, and what is out of range', () {
      final packet = Dmx.encode(DmxFrame(universe: 5, length: 10, values: Uint8List.fromList([1, 2, 3])));
      expect(packet.length, 28);
      final back = Dmx.decode(packet, sacn: false);
      expect(back.universe, 5);
      expect(back.length, 10);
      expect(back.values.sublist(0, 3), [1, 2, 3]);
      expect(() => Dmx.encode(DmxFrame(sacn: true, universe: 0)), throwsA(isA<PxsException>()));
    });
  });

  group('MIDI', () {
    test('built is read back', () {
      final cc = Midi.controlChange(3, 7, 1.0);
      expect(cc, [0xB2, 7, 127]);
      final m = Midi.parse(cc)!;
      expect(m.kind, MidiKind.controlChange);
      expect(m.channel, 3);
      expect(m.number, 7);
      expect(m.value, 1.0);
      expect(Midi.parse(Midi.noteOff(1, 60))!.value, 0.0);
      expect(Midi.parse(Midi.pitchBend(2, 0.5))!.kind, MidiKind.pitchBend);
      expect(Midi.parse(Midi.programChange(1, 12))!.number, 12);
      expect(Midi.parse(Uint8List.fromList([0xF8])), isNull); // a clock is not read
    });
  });

  test('the WebSocket handshake of RFC 6455', () {
    expect(WebSocket.acceptKey('dGhlIHNhbXBsZSBub25jZQ=='), 's3pPLMBiTxaQ9kYGzzhZRbK+xOo=');
  });
}
