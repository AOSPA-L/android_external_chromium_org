# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Scirra WebGL and Canvas2D rendering benchmark suite.

The Scirra WebGL performance test measures the number of 2D triangles
represented onscreen when the animation reaches the 30 FPS threshold.
"""

import os

from telemetry import test
from telemetry.page import page_measurement
from telemetry.page import page_set


class ScirraMeasurement(page_measurement.PageMeasurement):

  def WillNavigateToPage(self, page, tab):
    page.script_to_evaluate_on_commit = 'window.sprites = 0;'

  def MeasurePage(self, _, tab, results):
    object_count = '$objectcount$'
    fps = '$fps$'
    tickcount = '$tickcount$'
    # For http://www.scirra.com/labs/renderperf3/, JavaScript generated by
    # Construct 2 has different variables for Objects, fps and tickcount.
    if 'renderperf3' in tab.url:
      object_count = '$d'
      fps = 'Rb'
      tickcount = 'Ff'

    # Updates object count variable, when the FPS reaches 30 threshold and
    # tickcounts to reach value greater than 500(just to stablize frames).
    js_is_done = """
        var IsTestDone = function() {
          if (window.cr_getC2Runtime().%(tickcount)s > 500 &&
              window.cr_getC2Runtime().%(fps)s == 30) {
            window.sprites = window.cr_getC2Runtime().%(object_count)s;
            return true;
          } else {
            return false;
          }
        };
        IsTestDone();
        """ % {'tickcount': tickcount, 'fps': fps, 'object_count': object_count}
    tab.WaitForJavaScriptExpression(js_is_done, 300)
    total = int(tab.EvaluateJavaScript('window.sprites'))
    results.Add('Count', 'count', total)


class ScirraBenchmark(test.Test):
  """WebGL and Canvas2D rendering benchmark suite."""
  test = ScirraMeasurement
  def CreatePageSet(self, options):
    return page_set.PageSet.FromDict({
        'archive_data_file': '../page_sets/data/scirra.json',
        'make_javascript_deterministic': False,
        'pages': [
            { 'url': 'http://www.scirra.com/labs/renderperf3/'},
            { 'url': 'http://www.scirra.com/demos/c2/renderperfgl/'},
            { 'url': 'http://www.scirra.com/demos/c2/renderperf2d/'}
          ]
        }, os.path.abspath(__file__))
