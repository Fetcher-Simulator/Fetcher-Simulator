"""Regress the source MIDI's six named tracks sharing a single channel."""
import importlib.util
import io
from pathlib import Path
import unittest
import mido

spec = importlib.util.spec_from_file_location('builder', Path(__file__).parents[1]/'procedural-synth-assets/build-guitar-techs-banks.py')
builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)


class SourceMapping(unittest.TestCase):
    def source(self):
        midi = mido.MidiFile(ticks_per_beat=960)
        midi.tracks.append(mido.MidiTrack([mido.MetaMessage('track_name', name='AllNotes'),
            mido.MetaMessage('set_tempo', tempo=500000), mido.MetaMessage('set_tempo', tempo=1000000, time=960)]))
        for name, root in [('E',40),('A',45),('D',50),('G',55),('B',59),('e',64)]:
            track = mido.MidiTrack([mido.MetaMessage('track_name', name=name),
                mido.Message('note_on', channel=0, note=root, velocity=100, time=1920),
                mido.Message('note_off', channel=0, note=root, time=100)])
            if name in ('E','A','D'):
                track.append(mido.Message('note_on', channel=0, note=52, velocity=100, time=100))
            if name == 'e':
                track.append(mido.Message('note_on', channel=0, note=60, velocity=120, time=100)) # impossible below open pitch
            if name == 'B':
                track.append(mido.Message('note_on', channel=0, note=60, velocity=10, time=100)) # pickup ghost
                track.append(mido.Message('note_on', channel=0, note=60, velocity=127, time=100))
            midi.tracks.append(track)
        output = io.BytesIO(); midi.save(file=output); return output.getvalue()

    def test_physical_strings_not_channels(self):
        events = builder.string_events(self.source(), 11)
        self.assertEqual({e[2] for e in events}, set(range(1,7)))
        selected = builder.closest_fret_event(events, 52)
        self.assertEqual(selected[2:4], (3,'D')) # E fret 12, A fret 7, D fret 2
        self.assertEqual(builder.closest_fret_event(events, 64)[2:4], (6,'e'))

    def test_tempo_ghosts_and_unplayable_labels(self):
        events = builder.string_events(self.source(), 11)
        self.assertAlmostEqual(next(e[0] for e in events if e[1]==40), 1.5)
        selected = builder.closest_fret_event(events, 60)
        self.assertEqual(selected[3:], ('B',127))
        self.assertTrue(all(e[4] > 10 for e in events))


if __name__ == '__main__': unittest.main()
