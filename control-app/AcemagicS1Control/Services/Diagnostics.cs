using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace AcemagicS1Control.Services;

public sealed record MetricSample(DateTime Time, long Produced, long Consumed, long Dropped, long PublishedSeq);

/// <summary>
/// Computes frame-rate and drop-rate metrics from successive reads of the IPC
/// header counters. Used for the diagnostics panel.
/// </summary>
public sealed class Diagnostics
{
    private readonly List<MetricSample> _samples = new();
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private MetricSample? _last;

    public double ProducedFps { get; private set; }
    public double ConsumedFps { get; private set; }
    public double DropRatio { get; private set; }
    public double ProducerAgeMs { get; private set; }
    public double ConsumerAgeMs { get; private set; }

    public void Update(IpcHeader h)
    {
        var now = DateTime.UtcNow;
        var sample = new MetricSample(now, h.FramesProduced, h.FramesConsumed, h.FramesDropped, h.PublishedSeq);

        if (_last is { } prev)
        {
            var dt = (sample.Time - prev.Time).TotalSeconds;
            if (dt > 0.05)
            {
                ProducedFps = Math.Max(0, (sample.Produced - prev.Produced) / dt);
                ConsumedFps = Math.Max(0, (sample.Consumed - prev.Consumed) / dt);
                var produced = Math.Max(1, sample.Produced);
                DropRatio = (double)sample.Dropped / Math.Max(produced, 1);
            }
        }
        _last = sample;
        _samples.Add(sample);
        if (_samples.Count > 600) _samples.RemoveAt(0);

        // Freshness of producer/consumer heartbeats (both use GetTickCount64).
        var tick = Environment.TickCount64;
        ProducerAgeMs = h.LastProduceQpc > 0 ? tick - h.LastProduceQpc : double.PositiveInfinity;
        ConsumerAgeMs = h.LastConsumeQpc > 0 ? tick - h.LastConsumeQpc : double.PositiveInfinity;
    }

    public IReadOnlyList<MetricSample> History => _samples;
}
