"""Regression checks for time alignment, missing history, and forecast targets."""
import unittest

import numpy as np
import pandas as pd

from Code.forecasting.forecast import SENSORS, features, minute_data, predict, supervised


class ConstantModel:
    def __init__(self, value):
        self.value = value

    def predict(self, x):
        return np.full(len(x), self.value)


class ForecastTests(unittest.TestCase):
    def setUp(self):
        self.index = pd.date_range('2026-01-01', periods=100, freq='min', tz='UTC')
        self.minutes = pd.DataFrame({s: np.arange(100, dtype=float) for s in SENSORS}, index=self.index)

    def test_targets_and_future_changes(self):
        x, y = supervised(self.minutes)
        origin = self.index[40]
        self.assertEqual(y.loc[origin, 15], 55)
        self.assertEqual(x.loc[origin, 'moisture_lag_0'], 40)
        changed = self.minutes.copy()
        changed.loc[changed.index > origin] = -999
        pd.testing.assert_series_equal(features(self.minutes).loc[origin], features(changed).loc[origin])

    def test_gaps_break_history_and_future_labels(self):
        self.minutes.loc[self.index[45]] = np.nan
        x, _ = supervised(self.minutes)
        self.assertNotIn(self.index[40], x.index)  # future crosses outage
        self.assertNotIn(self.index[60], x.index)  # history crosses outage
        self.assertIn(self.index[80], x.index)

    def test_bins_are_right_labelled_and_sparse_minutes_rejected(self):
        index = pd.to_datetime(['2026-01-01T00:00:05Z', '2026-01-01T00:00:20Z',
                                '2026-01-01T00:00:40Z', '2026-01-01T00:01:05Z'])
        raw = pd.DataFrame({s: [10., 20., 30., 90.] for s in SENSORS}, index=index)
        result = minute_data(raw)
        self.assertEqual(result.iloc[0].moisture, 20)
        self.assertEqual(result.index[0], pd.Timestamp('2026-01-01T00:01:00Z'))
        self.assertTrue(result.iloc[1].isna().all())

    def test_prediction_requires_current_complete_history(self):
        artifact = {'device': 'test', 'models': {5: None, 10: None, 15: None},
                    'feature_names': list(features(self.minutes).columns)}
        result = predict(artifact, self.minutes, self.index[-1])
        self.assertEqual(result['forecasts']['15m']['moisture'], 99)
        self.assertEqual(pd.Timestamp(result['forecasts']['15m']['target_time_utc']),
                         self.index[-1] + pd.Timedelta(minutes=15))
        self.minutes.iloc[-1] = np.nan
        with self.assertRaises(ValueError):
            predict(artifact, self.minutes, self.index[-1])

    def test_all_models_keep_distinct_values_and_target_times(self):
        artifact = {'device': 'test', 'models': {h: None for h in [5, 10, 15]},
                    'feature_names': list(features(self.minutes).columns),
                    'ml_candidates': {h: {'random_forest': ConstantModel(50 + h),
                                          'gradient_boosting': ConstantModel(60 + h)}
                                      for h in [5, 10, 15]}}
        result = predict(artifact, self.minutes, self.index[-1])
        self.assertEqual(len(result['model_forecasts']), 9)
        for record in result['model_forecasts']:
            h = int(record['horizon'][:-1])
            self.assertEqual(record['target_time_utc'], result['forecasts'][f'{h}m']['target_time_utc'])
            expected = {'persistence': 99, 'random_forest': 50 + h, 'gradient_boosting': 60 + h}
            self.assertEqual(record['moisture'], expected[record['model']])


if __name__ == '__main__':
    unittest.main()
