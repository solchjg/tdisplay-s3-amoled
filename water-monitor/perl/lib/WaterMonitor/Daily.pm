package WaterMonitor::Daily;

# Daily lows, highs and averages per US Eastern day (pure).
# The Perl twin of water_monitor/daily.py.

use strict;
use warnings;
use DateTime;
use DateTime::TimeZone;
use List::Util qw(sum);
use Exporter 'import';

our @EXPORT_OK = qw(eastern daily missing_reports day_start_epoch FULL_DAY_MIN);

use constant FULL_DAY_MIN  => 20;   # fewer samples mark a day partial
use constant LEVEL_STEP_FT => 1.0;  # a larger jump in one sample is flagged

my $TZ = DateTime::TimeZone->new(name => 'America/New_York');

# Values outside these are kept but flagged; cond 0 = sensor out of water.
my @LIMITS = ([temp_f => 32, 90], [cond_us_cm => 0.001, undef]);

# Epoch seconds as an Eastern DateTime.
sub eastern { DateTime->from_epoch(epoch => $_[0], time_zone => $TZ) }

# Eastern midnight of 'YYYY-MM-DD' as epoch seconds.
sub day_start_epoch {
    my ($day) = @_;
    my ($y, $m, $d) = split /-/, $day;
    return DateTime->new(year => $y, month => $m, day => $d,
                         time_zone => $TZ)->epoch;
}

sub _g { my $v = shift; return 0 + sprintf('%.10g', $v) }

sub _flags {
    my ($rows) = @_;
    my (@out, $prev);
    for my $r (@$rows) {
        my ($ts, $values) = @$r;
        my $when = eastern($ts)->strftime('%H:%M');
        for my $l (@LIMITS) {
            my ($key, $lo, $hi) = @$l;
            my $v = $values->{$key} // next;
            push @out, sprintf('%s %s low at %s',  $key, _g($v), $when)
                if defined $lo && $v < $lo;
            push @out, sprintf('%s %s high at %s', $key, _g($v), $when)
                if defined $hi && $v > $hi;
        }
        my $level = $values->{level_ft};
        if (defined $level && defined $prev && abs($level - $prev) > LEVEL_STEP_FT) {
            push @out, sprintf('level_ft step %+.2f at %s', $level - $prev, $when);
        }
        $prev = $level if defined $level;
    }
    return \@out;
}

# [[epoch, {key => value}], ...] grouped by Eastern day, days ascending:
# [{day => 'YYYY-MM-DD', count, partial, flags => [...],
#   stats => {key => {min, max, avg, min_at, max_at}}}]  (times HH:MM ET)
sub daily {
    my ($rows) = @_;
    my @sorted = sort { $a->[0] <=> $b->[0] } @$rows;
    my %by_day;
    push @{ $by_day{ eastern($_->[0])->ymd } }, $_ for @sorted;
    my @days;
    for my $day (sort keys %by_day) {
        my $group = $by_day{$day};
        my %keys = map { %{ $_->[1] } } @$group;
        my %stats;
        for my $key (sort keys %keys) {
            my @pts = map { [$_->[0], $_->[1]{$key}] }
                      grep { defined $_->[1]{$key} } @$group;
            my ($lo, $hi) = ($pts[0], $pts[0]);
            for my $p (@pts) {   # first of equals wins, as in Python
                $lo = $p if $p->[1] < $lo->[1];
                $hi = $p if $p->[1] > $hi->[1];
            }
            $stats{$key} = {
                min => $lo->[1], max => $hi->[1],
                avg => sum(map { $_->[1] } @pts) / @pts,
                min_at => eastern($lo->[0])->strftime('%H:%M'),
                max_at => eastern($hi->[0])->strftime('%H:%M'),
            };
        }
        push @days, {day => $day, count => scalar @$group,
                     partial => (@$group < FULL_DAY_MIN ? 1 : 0),
                     stats => \%stats, flags => _flags($group)};
    }
    return \@days;
}

# Report numbers absent between the lowest and highest seen.
sub missing_reports {
    my %seen = map { $_ => 1 } grep { defined } @_;
    return () unless %seen;
    my @n = sort { $a <=> $b } keys %seen;
    return grep { !$seen{$_} } $n[0] .. $n[-1];
}

1;
