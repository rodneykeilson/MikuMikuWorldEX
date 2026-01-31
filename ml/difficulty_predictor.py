"""
MikuMikuWorld Difficulty Predictor
Trains a machine learning model to predict chart difficulty levels from CCMMWS files.

Usage:
    python difficulty_predictor.py train    # Train the model
    python difficulty_predictor.py predict <ccmmws_file>  # Predict difficulty
"""

import struct
import json
import os
import sys
import pickle
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional
import numpy as np

# Try to import ML libraries
try:
    import xgboost as xgb
    HAS_XGBOOST = True
except ImportError:
    HAS_XGBOOST = False

try:
    from sklearn.ensemble import RandomForestRegressor, GradientBoostingRegressor
    from sklearn.model_selection import train_test_split, cross_val_score
    from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
    from sklearn.preprocessing import StandardScaler
    HAS_SKLEARN = True
except ImportError:
    HAS_SKLEARN = False


# CCMMWS Binary Format Structures
@dataclass
class Note:
    tick: int
    lane: int
    width: int
    note_type: int  # 0=Tap, 1=Hold, 2=HoldMid, 3=HoldEnd, 4=Damage
    critical: bool
    friction: bool
    flick: int  # 0=None, 1=Default, 2=Left, 3=Right
    layer: int = 0


@dataclass
class HoldNote:
    start: Note
    end: Note
    steps: List[Note] = field(default_factory=list)
    fade_type: int = 0
    guide_color: int = 0
    is_guide: bool = False


@dataclass
class ScoreData:
    """Parsed CCMMWS score data"""
    ticks_per_beat: int = 480
    notes: List[Note] = field(default_factory=list)
    holds: List[HoldNote] = field(default_factory=list)
    bpm_changes: List[Tuple[int, float]] = field(default_factory=list)


class CCMMWSParser:
    """Parser for CCMMWS binary format"""
    
    def __init__(self, filepath: str):
        self.filepath = filepath
        self.data = None
        self.pos = 0
        
    def read_uint8(self) -> int:
        val = struct.unpack('<B', self.data[self.pos:self.pos+1])[0]
        self.pos += 1
        return val
        
    def read_uint16(self) -> int:
        val = struct.unpack('<H', self.data[self.pos:self.pos+2])[0]
        self.pos += 2
        return val
        
    def read_uint32(self) -> int:
        val = struct.unpack('<I', self.data[self.pos:self.pos+4])[0]
        self.pos += 4
        return val
        
    def read_int32(self) -> int:
        val = struct.unpack('<i', self.data[self.pos:self.pos+4])[0]
        self.pos += 4
        return val
        
    def read_float(self) -> float:
        val = struct.unpack('<f', self.data[self.pos:self.pos+4])[0]
        self.pos += 4
        return val
        
    def read_string(self) -> str:
        """Read null-terminated string"""
        chars = []
        while self.pos < len(self.data):
            c = self.data[self.pos]
            self.pos += 1
            if c == 0:
                break
            chars.append(chr(c))
        return ''.join(chars)
        
    def seek(self, pos: int):
        self.pos = pos
        
    def read_note(self, note_type: int, cyanvas_version: int) -> Note:
        """
        Read a note from binary data.
        note_type: 0=Tap, 1=Hold, 2=HoldMid, 3=HoldEnd, 4=Damage
        """
        tick = self.read_uint32()
        
        if cyanvas_version <= 5:
            lane = self.read_int32()  # int32 in older versions
            width = self.read_uint32()  # uint32 in older versions
        else:
            lane = int(self.read_float())  # float in newer versions
            width = int(self.read_float())
        
        layer = 0
        if cyanvas_version >= 4:
            layer = self.read_uint32()
        
        # hasEase() returns true for Hold and HoldMid types
        has_ease = note_type in (1, 2)  # Hold or HoldMid
        
        flick = 0
        if not has_ease:
            flick = self.read_uint32()
        
        flags = self.read_uint32()
        critical = (flags & 1) != 0  # NOTE_CRITICAL = 1
        friction = (flags & 2) != 0  # NOTE_FRICTION = 2
            
        return Note(
            tick=tick,
            lane=lane,
            width=width,
            note_type=note_type,
            critical=critical,
            friction=friction,
            flick=flick,
            layer=layer
        )
        
    def parse(self) -> Optional[ScoreData]:
        try:
            with open(self.filepath, 'rb') as f:
                self.data = f.read()
        except Exception as e:
            print(f"Error reading file: {e}")
            return None
            
        self.pos = 0
        score = ScoreData()
        
        # Read signature (null-terminated string)
        signature = self.read_string()
        
        if signature not in ('MMWS', 'CCMMWS'):
            print(f"Invalid signature: {signature}")
            return None
            
        is_cyanvas = signature == 'CCMMWS'
        
        version = self.read_uint16()
        cyanvas_version = self.read_uint16()
        if is_cyanvas and cyanvas_version == 0:
            cyanvas_version = 1
            
        # Read address table
        if version > 2:
            metadata_addr = self.read_uint32()
            events_addr = self.read_uint32()
            taps_addr = self.read_uint32()
            holds_addr = self.read_uint32()
            damages_addr = self.read_uint32() if is_cyanvas else 0
            layers_addr = self.read_uint32() if cyanvas_version >= 4 else 0
            waypoints_addr = self.read_uint32() if cyanvas_version >= 5 else 0
            
            # Read events (BPM changes) - we only need BPMs for timing
            self.seek(events_addr)
            bpm_count = self.read_uint32()
            for _ in range(bpm_count):
                tick = self.read_uint32()  # uint32, not int32
                bpm = self.read_float()
                score.bpm_changes.append((tick, bpm))
                
            # Skip time signatures and hi-speed changes - use seek to jump directly to taps
            
            # Read taps
            self.seek(taps_addr)
            tap_count = self.read_uint32()
            for _ in range(tap_count):
                note = self.read_note(0, cyanvas_version)
                score.notes.append(note)
                
            # Read holds
            self.seek(holds_addr)
            hold_count = self.read_uint32()
            for _ in range(hold_count):
                flags = self.read_uint32() if version > 3 else 0
                
                start = self.read_note(1, cyanvas_version)
                ease = self.read_uint32()
                fade_type = self.read_uint32() if cyanvas_version >= 2 else 0
                guide_color = self.read_uint32() if cyanvas_version >= 3 else 0
                
                steps = []
                step_count = self.read_uint32()
                for _ in range(step_count):
                    step_note = self.read_note(2, cyanvas_version)
                    step_type = self.read_uint32()
                    step_ease = self.read_uint32()
                    steps.append(step_note)
                    
                end = self.read_note(3, cyanvas_version)
                
                hold = HoldNote(
                    start=start,
                    end=end,
                    steps=steps,
                    fade_type=fade_type,
                    guide_color=guide_color,
                    is_guide=(flags & 4) != 0
                )
                score.holds.append(hold)
                score.notes.append(start)
                score.notes.extend(steps)
                score.notes.append(end)
                
            # Read damage notes
            if is_cyanvas and damages_addr > 0:
                self.seek(damages_addr)
                damage_count = self.read_uint32()
                for _ in range(damage_count):
                    note = self.read_note(4, cyanvas_version)
                    score.notes.append(note)
                    
        return score


class FeatureExtractor:
    """Extract ML features from parsed CCMMWS data"""
    
    TICKS_PER_BEAT = 480
    
    def __init__(self):
        self.feature_names = []
        
    def tick_to_seconds(self, tick: int, bpm_changes: List[Tuple[int, float]]) -> float:
        """Convert tick to seconds using BPM changes"""
        if not bpm_changes:
            return tick / self.TICKS_PER_BEAT / 120 * 60  # Default 120 BPM
            
        seconds = 0.0
        prev_tick = 0
        prev_bpm = bpm_changes[0][1] if bpm_changes else 120
        
        for change_tick, bpm in bpm_changes:
            if change_tick >= tick:
                break
            seconds += (change_tick - prev_tick) / self.TICKS_PER_BEAT / prev_bpm * 60
            prev_tick = change_tick
            prev_bpm = bpm
            
        seconds += (tick - prev_tick) / self.TICKS_PER_BEAT / prev_bpm * 60
        return seconds
        
    def extract(self, score: ScoreData) -> Dict[str, float]:
        """Extract features from score data"""
        features = {}
        
        if not score.notes:
            return self._empty_features()
            
        # Sort notes by tick
        notes = sorted(score.notes, key=lambda n: n.tick)
        
        # Basic counts
        total_notes = len(notes)
        tap_notes = [n for n in notes if n.note_type == 0]
        hold_starts = [n for n in notes if n.note_type == 1]
        hold_mids = [n for n in notes if n.note_type == 2]
        hold_ends = [n for n in notes if n.note_type == 3]
        damage_notes = [n for n in notes if n.note_type == 4]
        
        features['total_notes'] = total_notes
        features['tap_count'] = len(tap_notes)
        features['hold_count'] = len(score.holds)
        features['hold_step_count'] = len(hold_mids)
        features['damage_count'] = len(damage_notes)
        
        # Critical and flick notes
        critical_notes = [n for n in notes if n.critical]
        flick_notes = [n for n in notes if n.flick > 0]
        friction_notes = [n for n in notes if n.friction]
        
        features['critical_count'] = len(critical_notes)
        features['critical_ratio'] = len(critical_notes) / max(total_notes, 1)
        features['flick_count'] = len(flick_notes)
        features['flick_ratio'] = len(flick_notes) / max(total_notes, 1)
        features['friction_count'] = len(friction_notes)
        features['friction_ratio'] = len(friction_notes) / max(total_notes, 1)
        
        # Duration and timing
        if notes:
            min_tick = min(n.tick for n in notes)
            max_tick = max(n.tick for n in notes)
            duration_ticks = max_tick - min_tick
            duration_sec = self.tick_to_seconds(max_tick, score.bpm_changes) - \
                          self.tick_to_seconds(min_tick, score.bpm_changes)
            duration_sec = max(duration_sec, 1.0)
        else:
            duration_ticks = 0
            duration_sec = 1.0
            
        features['duration_seconds'] = duration_sec
        features['duration_ticks'] = duration_ticks
        
        # Notes per second (NPS)
        features['avg_nps'] = total_notes / max(duration_sec, 1)
        
        # Peak NPS (sliding window)
        window_sec = 1.0
        peak_nps = self._calculate_peak_nps(notes, score.bpm_changes, window_sec)
        features['peak_nps_1s'] = peak_nps
        
        window_sec = 2.0
        peak_nps_2s = self._calculate_peak_nps(notes, score.bpm_changes, window_sec)
        features['peak_nps_2s'] = peak_nps_2s
        
        window_sec = 5.0
        peak_nps_5s = self._calculate_peak_nps(notes, score.bpm_changes, window_sec)
        features['peak_nps_5s'] = peak_nps_5s
        
        # BPM features
        if score.bpm_changes:
            bpms = [bpm for _, bpm in score.bpm_changes]
            features['bpm_min'] = min(bpms)
            features['bpm_max'] = max(bpms)
            features['bpm_avg'] = np.mean(bpms)
            features['bpm_std'] = np.std(bpms) if len(bpms) > 1 else 0
            features['bpm_change_count'] = len(bpms) - 1
        else:
            features['bpm_min'] = 120
            features['bpm_max'] = 120
            features['bpm_avg'] = 120
            features['bpm_std'] = 0
            features['bpm_change_count'] = 0
            
        # Lane usage (0-11 lanes)
        lane_counts = [0] * 12
        for note in notes:
            lane = max(0, min(11, note.lane))
            for i in range(note.width):
                if lane + i < 12:
                    lane_counts[lane + i] += 1
                    
        features['lane_spread'] = sum(1 for c in lane_counts if c > 0)
        features['lane_std'] = np.std(lane_counts)
        features['lane_center_usage'] = sum(lane_counts[4:8]) / max(sum(lane_counts), 1)
        features['lane_edge_usage'] = (sum(lane_counts[0:2]) + sum(lane_counts[10:12])) / max(sum(lane_counts), 1)
        
        # Width features
        widths = [n.width for n in notes]
        features['width_avg'] = np.mean(widths)
        features['width_max'] = max(widths)
        features['width_std'] = np.std(widths)
        features['wide_note_count'] = sum(1 for w in widths if w >= 4)
        features['wide_note_ratio'] = features['wide_note_count'] / max(total_notes, 1)
        
        # Tick spacing (rhythm complexity)
        if len(notes) > 1:
            ticks = sorted([n.tick for n in notes])
            spacings = [ticks[i+1] - ticks[i] for i in range(len(ticks)-1)]
            spacings = [s for s in spacings if s > 0]  # Remove simultaneous notes
            
            if spacings:
                features['spacing_min'] = min(spacings)
                features['spacing_max'] = max(spacings)
                features['spacing_avg'] = np.mean(spacings)
                features['spacing_std'] = np.std(spacings)
                
                # Count different beat divisions (rhythm variety)
                beat_divisions = set()
                for s in spacings:
                    for div in [1, 2, 3, 4, 6, 8, 12, 16, 24, 32]:
                        if abs(s - self.TICKS_PER_BEAT / div) < 10:
                            beat_divisions.add(div)
                features['rhythm_variety'] = len(beat_divisions)
            else:
                features['spacing_min'] = 0
                features['spacing_max'] = 0
                features['spacing_avg'] = 0
                features['spacing_std'] = 0
                features['rhythm_variety'] = 0
        else:
            features['spacing_min'] = 0
            features['spacing_max'] = 0
            features['spacing_avg'] = 0
            features['spacing_std'] = 0
            features['rhythm_variety'] = 0
            
        # Simultaneous notes (chords)
        tick_groups = {}
        for note in notes:
            if note.tick not in tick_groups:
                tick_groups[note.tick] = []
            tick_groups[note.tick].append(note)
            
        chord_counts = [len(g) for g in tick_groups.values()]
        features['chord_count'] = sum(1 for c in chord_counts if c > 1)
        features['chord_ratio'] = features['chord_count'] / max(len(tick_groups), 1)
        features['max_simultaneous'] = max(chord_counts) if chord_counts else 0
        features['avg_simultaneous'] = np.mean(chord_counts) if chord_counts else 0
        
        # Hold note features
        if score.holds:
            hold_durations = []
            hold_step_counts = []
            for hold in score.holds:
                dur = hold.end.tick - hold.start.tick
                hold_durations.append(dur)
                hold_step_counts.append(len(hold.steps))
                
            features['hold_duration_avg'] = np.mean(hold_durations)
            features['hold_duration_max'] = max(hold_durations)
            features['hold_steps_avg'] = np.mean(hold_step_counts)
            features['hold_steps_max'] = max(hold_step_counts)
            
            guide_holds = [h for h in score.holds if h.is_guide]
            features['guide_hold_count'] = len(guide_holds)
        else:
            features['hold_duration_avg'] = 0
            features['hold_duration_max'] = 0
            features['hold_steps_avg'] = 0
            features['hold_steps_max'] = 0
            features['guide_hold_count'] = 0
            
        # Movement complexity (lane jumps)
        if len(notes) > 1:
            sorted_notes = sorted(notes, key=lambda n: n.tick)
            lane_jumps = []
            for i in range(len(sorted_notes) - 1):
                jump = abs(sorted_notes[i+1].lane - sorted_notes[i].lane)
                lane_jumps.append(jump)
                
            features['lane_jump_avg'] = np.mean(lane_jumps)
            features['lane_jump_max'] = max(lane_jumps)
            features['lane_jump_std'] = np.std(lane_jumps)
            features['large_jump_count'] = sum(1 for j in lane_jumps if j >= 6)
        else:
            features['lane_jump_avg'] = 0
            features['lane_jump_max'] = 0
            features['lane_jump_std'] = 0
            features['large_jump_count'] = 0
            
        # Density variation
        features['density_variation'] = self._calculate_density_variation(notes, score.bpm_changes)
        
        self.feature_names = list(features.keys())
        return features
        
    def _calculate_peak_nps(self, notes: List[Note], bpm_changes: List[Tuple[int, float]], 
                           window_sec: float) -> float:
        if not notes:
            return 0.0
            
        # Convert all notes to seconds
        note_times = [self.tick_to_seconds(n.tick, bpm_changes) for n in notes]
        note_times.sort()
        
        if not note_times or note_times[-1] - note_times[0] < window_sec:
            return len(notes) / max(note_times[-1] - note_times[0], 0.1)
            
        max_nps = 0.0
        for start_time in note_times:
            end_time = start_time + window_sec
            count = sum(1 for t in note_times if start_time <= t < end_time)
            nps = count / window_sec
            max_nps = max(max_nps, nps)
            
        return max_nps
        
    def _calculate_density_variation(self, notes: List[Note], 
                                    bpm_changes: List[Tuple[int, float]]) -> float:
        """Calculate how much the note density varies throughout the song"""
        if len(notes) < 10:
            return 0.0
            
        # Divide song into 10 segments
        note_times = sorted([self.tick_to_seconds(n.tick, bpm_changes) for n in notes])
        min_time = note_times[0]
        max_time = note_times[-1]
        duration = max_time - min_time
        
        if duration < 1.0:
            return 0.0
            
        segment_size = duration / 10
        segment_counts = [0] * 10
        
        for t in note_times:
            segment = min(9, int((t - min_time) / segment_size))
            segment_counts[segment] += 1
            
        return np.std(segment_counts) / max(np.mean(segment_counts), 1)
        
    def _empty_features(self) -> Dict[str, float]:
        """Return empty features for invalid files"""
        return {name: 0.0 for name in [
            'total_notes', 'tap_count', 'hold_count', 'hold_step_count', 'damage_count',
            'critical_count', 'critical_ratio', 'flick_count', 'flick_ratio',
            'friction_count', 'friction_ratio', 'duration_seconds', 'duration_ticks',
            'avg_nps', 'peak_nps_1s', 'peak_nps_2s', 'peak_nps_5s',
            'bpm_min', 'bpm_max', 'bpm_avg', 'bpm_std', 'bpm_change_count',
            'lane_spread', 'lane_std', 'lane_center_usage', 'lane_edge_usage',
            'width_avg', 'width_max', 'width_std', 'wide_note_count', 'wide_note_ratio',
            'spacing_min', 'spacing_max', 'spacing_avg', 'spacing_std', 'rhythm_variety',
            'chord_count', 'chord_ratio', 'max_simultaneous', 'avg_simultaneous',
            'hold_duration_avg', 'hold_duration_max', 'hold_steps_avg', 'hold_steps_max',
            'guide_hold_count', 'lane_jump_avg', 'lane_jump_max', 'lane_jump_std',
            'large_jump_count', 'density_variation'
        ]}


class DifficultyPredictor:
    """ML model for predicting chart difficulty"""
    
    def __init__(self, model_path: str = None):
        self.model = None
        self.scaler = None
        self.feature_names = None
        self.model_path = model_path or "difficulty_model.pkl"
        self.extractor = FeatureExtractor()
        
    def load_training_data(self, ccmmws_dir: str, stats_file: str) -> Tuple[np.ndarray, np.ndarray]:
        """Load and extract features from training data"""
        # Load difficulty stats
        with open(stats_file, 'r', encoding='utf-8') as f:
            stats = json.load(f)
            
        # Create mapping from filename to level
        level_map = {}
        for entry in stats:
            # Convert from JSON filename to ccmmws filename
            base = entry['filename'].replace('.json', '')
            level_map[base] = entry['level']
            
        print(f"Loaded {len(level_map)} difficulty entries")
        
        # Extract features from all ccmmws files
        X = []
        y = []
        processed = 0
        
        ccmmws_path = Path(ccmmws_dir)
        for filepath in ccmmws_path.rglob("*.ccmmws"):
            basename = filepath.stem  # e.g., "1_EASY"
            
            if basename not in level_map:
                continue
                
            # Parse and extract features
            parser = CCMMWSParser(str(filepath))
            score = parser.parse()
            
            if score is None:
                continue
                
            features = self.extractor.extract(score)
            X.append(list(features.values()))
            y.append(level_map[basename])
            
            processed += 1
            if processed % 100 == 0:
                print(f"Processed {processed} files...")
                
        print(f"Successfully extracted features from {len(X)} charts")
        self.feature_names = self.extractor.feature_names
        
        return np.array(X), np.array(y)
        
    def train(self, X: np.ndarray, y: np.ndarray, use_gpu: bool = True):
        """Train the difficulty prediction model"""
        print(f"Training on {len(X)} samples with {X.shape[1]} features")
        print(f"Level range: {y.min()} - {y.max()}")
        
        # Scale features
        self.scaler = StandardScaler()
        X_scaled = self.scaler.fit_transform(X)
        
        # Split data
        X_train, X_test, y_train, y_test = train_test_split(
            X_scaled, y, test_size=0.2, random_state=42
        )
        
        # Train model
        if HAS_XGBOOST and use_gpu:
            print("Training XGBoost model with GPU acceleration...")
            try:
                self.model = xgb.XGBRegressor(
                    n_estimators=200,
                    max_depth=8,
                    learning_rate=0.1,
                    tree_method='hist',
                    device='cuda',  # GPU acceleration
                    random_state=42
                )
                self.model.fit(X_train, y_train)
            except Exception as e:
                print(f"GPU training failed, falling back to CPU: {e}")
                self.model = xgb.XGBRegressor(
                    n_estimators=200,
                    max_depth=8,
                    learning_rate=0.1,
                    random_state=42
                )
                self.model.fit(X_train, y_train)
        elif HAS_XGBOOST:
            print("Training XGBoost model on CPU...")
            self.model = xgb.XGBRegressor(
                n_estimators=200,
                max_depth=8,
                learning_rate=0.1,
                random_state=42
            )
            self.model.fit(X_train, y_train)
        else:
            print("Training Gradient Boosting model (sklearn)...")
            self.model = GradientBoostingRegressor(
                n_estimators=200,
                max_depth=6,
                learning_rate=0.1,
                random_state=42
            )
            self.model.fit(X_train, y_train)
            
        # Evaluate
        y_pred = self.model.predict(X_test)
        mae = mean_absolute_error(y_test, y_pred)
        rmse = np.sqrt(mean_squared_error(y_test, y_pred))
        r2 = r2_score(y_test, y_pred)
        
        print(f"\nModel Performance:")
        print(f"  MAE:  {mae:.2f} levels")
        print(f"  RMSE: {rmse:.2f} levels")
        print(f"  R²:   {r2:.3f}")
        
        # Show feature importance
        if hasattr(self.model, 'feature_importances_'):
            importance = list(zip(self.feature_names, self.model.feature_importances_))
            importance.sort(key=lambda x: x[1], reverse=True)
            print(f"\nTop 10 Most Important Features:")
            for name, imp in importance[:10]:
                print(f"  {name}: {imp:.4f}")
                
        # Cross-validation
        print("\nCross-validation scores:")
        cv_scores = cross_val_score(self.model, X_scaled, y, cv=5, scoring='neg_mean_absolute_error')
        print(f"  CV MAE: {-cv_scores.mean():.2f} ± {cv_scores.std():.2f}")
        
    def save(self, filepath: str = None):
        """Save the trained model"""
        filepath = filepath or self.model_path
        with open(filepath, 'wb') as f:
            pickle.dump({
                'model': self.model,
                'scaler': self.scaler,
                'feature_names': self.feature_names
            }, f)
        print(f"Model saved to {filepath}")
        
    def load(self, filepath: str = None):
        """Load a trained model"""
        filepath = filepath or self.model_path
        with open(filepath, 'rb') as f:
            data = pickle.load(f)
            self.model = data['model']
            self.scaler = data['scaler']
            self.feature_names = data['feature_names']
        print(f"Model loaded from {filepath}")
        
    def predict(self, ccmmws_path: str) -> float:
        """Predict difficulty for a single chart"""
        if self.model is None:
            raise ValueError("Model not trained or loaded")
            
        parser = CCMMWSParser(ccmmws_path)
        score = parser.parse()
        
        if score is None:
            raise ValueError(f"Failed to parse {ccmmws_path}")
            
        features = self.extractor.extract(score)
        X = np.array([list(features.values())])
        X_scaled = self.scaler.transform(X)
        
        prediction = self.model.predict(X_scaled)[0]
        return round(prediction, 1)
        
    def predict_with_details(self, ccmmws_path: str) -> Dict:
        """Predict difficulty with additional details"""
        if self.model is None:
            raise ValueError("Model not trained or loaded")
            
        parser = CCMMWSParser(ccmmws_path)
        score = parser.parse()
        
        if score is None:
            raise ValueError(f"Failed to parse {ccmmws_path}")
            
        features = self.extractor.extract(score)
        X = np.array([list(features.values())])
        X_scaled = self.scaler.transform(X)
        
        prediction = self.model.predict(X_scaled)[0]
        
        return {
            'predicted_level': round(prediction, 1),
            'predicted_level_int': round(prediction),
            'features': features,
            'total_notes': int(features.get('total_notes', 0)),
            'avg_nps': round(features.get('avg_nps', 0), 2),
            'peak_nps': round(features.get('peak_nps_1s', 0), 2),
            'duration_sec': round(features.get('duration_seconds', 0), 1)
        }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
        
    command = sys.argv[1]
    
    # Get paths relative to this script
    script_dir = Path(__file__).parent
    project_dir = script_dir.parent
    data_dir = project_dir / "data"
    ccmmws_dir = data_dir / "ccmmws"
    stats_file = data_dir / "difficulty_stats.json"
    model_path = script_dir / "difficulty_model.pkl"
    
    predictor = DifficultyPredictor(str(model_path))
    
    if command == "train":
        if not HAS_SKLEARN:
            print("ERROR: scikit-learn is required. Install with: pip install scikit-learn")
            return
            
        print("Loading training data...")
        X, y = predictor.load_training_data(str(ccmmws_dir), str(stats_file))
        
        print("\nTraining model...")
        predictor.train(X, y, use_gpu=HAS_XGBOOST)
        
        print("\nSaving model...")
        predictor.save()
        
    elif command == "predict":
        if len(sys.argv) < 3:
            print("Usage: python difficulty_predictor.py predict <ccmmws_file>")
            return
            
        ccmmws_path = sys.argv[2]
        predictor.load()
        
        result = predictor.predict_with_details(ccmmws_path)
        print(f"\nPredicted Difficulty: Level {result['predicted_level_int']} ({result['predicted_level']:.1f})")
        print(f"Total Notes: {result['total_notes']}")
        print(f"Average NPS: {result['avg_nps']}")
        print(f"Peak NPS: {result['peak_nps']}")
        print(f"Duration: {result['duration_sec']}s")
        
    elif command == "test":
        # Test on a random sample
        predictor.load()
        
        import random
        ccmmws_files = list(ccmmws_dir.rglob("*.ccmmws"))
        sample = random.sample(ccmmws_files, min(10, len(ccmmws_files)))
        
        print("\nTesting on random samples:")
        for filepath in sample:
            try:
                result = predictor.predict_with_details(str(filepath))
                print(f"  {filepath.name}: Level {result['predicted_level_int']}")
            except Exception as e:
                print(f"  {filepath.name}: Error - {e}")
                
    else:
        print(f"Unknown command: {command}")
        print(__doc__)


if __name__ == "__main__":
    main()
