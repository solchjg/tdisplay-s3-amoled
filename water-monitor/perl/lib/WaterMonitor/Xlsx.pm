package WaterMonitor::Xlsx;

# The spreadsheet as .xlsx: a Summary sheet and one tab per site, each with
# four trend charts beside its table. The same sheets, columns and charts as
# the Python version's water-report.xlsx (water_monitor/workbook.py).
# build() makes the sheets as plain rows; write_xlsx() writes them.

use strict;
use warnings;
use Excel::Writer::XLSX;
use WaterMonitor::Daily qw(eastern daily missing_reports day_start_epoch);
use Exporter 'import';

our @EXPORT_OK = qw(build write_xlsx sheet_name);

our $NOTE = 'Times are US Eastern (America/New_York). Level is as the logger '
          . 'reports it, not corrected for air pressure.';

my @KEYS = (['level_ft', 'Level', 'ft'], ['temp_f', 'Temp', "\x{b0}F"],
            ['cond_us_cm', 'Cond', "\x{b5}S/cm"]);

# Charts on each site tab: [title, y axis, 0-based columns of site_rows].
our @SITE_CHARTS = (
    ['Level, daily low / high / average',        'ft',          [3, 4, 5]],
    ['Temperature, daily low / high / average',  "\x{b0}F",     [8, 9, 10]],
    ['Conductivity, daily low / high / average', "\x{b5}S/cm",  [11, 12, 13]],
    ['LevelSender battery',                      '%',           [14]],
);

# Excel's limits: 31 characters, none of []:*?/\
sub sheet_name {
    my ($t) = @_;
    $t =~ s{[\[\]:*?/\\]}{-}g;
    $t = substr($t, 0, 31);
    return length $t ? $t : 'Site';
}

sub _round { defined $_[0] ? 0 + sprintf('%.4f', $_[0]) : undef }

sub _et { defined $_[0] ? eastern($_[0])->strftime('%Y-%m-%d %H:%M') : undef }

# Rows of a site tab; dates as 'YYYY-MM-DD' strings in a {date => ...} cell.
sub site_rows {
    my ($days, $reports) = @_;
    my @head = ('Date', 'Samples', 'Partial');
    for my $k (@KEYS) {
        my (undef, $label, $unit) = @$k;
        push @head, "$label min ($unit)", "$label max ($unit)", "$label avg ($unit)";
        push @head, 'Level high at', 'Level low at' if $label eq 'Level';
    }
    push @head, 'Battery (%)', 'Signal', 'Report', 'Flags';
    my %by_day;   # the day's last report wins
    for my $r (@$reports) {
        next unless defined $r->{last_sample_epoch};
        $by_day{ eastern($r->{last_sample_epoch})->ymd } = $r;
    }
    my @rows = (\@head);
    for my $d (sort { $b->{day} cmp $a->{day} } @$days) {
        my @row = ({date => $d->{day}}, $d->{count}, $d->{partial} ? 'yes' : '');
        for my $k (@KEYS) {
            my ($key, $label) = @$k;
            my $s = $d->{stats}{$key};
            push @row, $s ? (_round($s->{min}), _round($s->{max}), _round($s->{avg}))
                          : (undef, undef, undef);
            push @row, $s ? ($s->{max_at}, $s->{min_at}) : (undef, undef)
                if $label eq 'Level';
        }
        my $r = $by_day{ $d->{day} };
        push @row, $r ? ($r->{battery_pct}, $r->{signal}, $r->{report_no})
                      : (undef, undef, undef),
                   join('; ', @{ $d->{flags} });
        push @rows, \@row;
    }
    return \@rows;
}

sub _window {
    my ($samples, $since, $key) = @_;
    my @pts = grep { $_->[0] >= $since && defined $_->[1]{$key} } @$samples;
    return (undef, undef, undef) unless @pts;
    my @v = map { $_->[1]{$key} } @pts;
    my ($lo, $hi) = ($v[0], $v[0]);
    for (@v) { $lo = $_ if $_ < $lo; $hi = $_ if $_ > $hi }
    return ($lo, $hi, $v[-1] - $v[0]);
}

sub summary_rows {
    my ($sites, $now) = @_;
    my @head = ('Site', 'Location', 'Last report (ET)', 'Report',
                'Missing reports', 'Battery (%)', 'Signal', 'Hours since report');
    for my $n (7, 30) {
        push @head, "Level low ${n}d (ft)", "Level high ${n}d (ft)",
            "Level change ${n}d (ft)", "Temp low ${n}d (\x{b0}F)",
            "Temp high ${n}d (\x{b0}F)", "Cond low ${n}d (\x{b5}S/cm)",
            "Cond high ${n}d (\x{b5}S/cm)";
    }
    my @rows = (\@head);
    my $today = eastern($now);
    for my $s (@$sites) {
        my ($serial, $location, $samples, $reports) = @$s;
        my $last = @$reports ? $reports->[-1] : {};
        my @nums = grep { defined } map { $_->{report_no} } @$reports;
        my $got = $last->{email_epoch} // $last->{last_sample_epoch};
        my @missing = missing_reports(@nums);
        my @row = ({str => $serial}, $location, _et($got), $last->{report_no},
                   scalar(@missing), $last->{battery_pct},
                   $last->{signal},
                   defined $got ? 0 + sprintf('%.1f', ($now - $got) / 3600) : undef);
        for my $n (7, 30) {
            my $start = day_start_epoch(
                $today->clone->subtract(days => $n - 1)->ymd);
            my ($lo, $hi, $ch) = _window($samples, $start, 'level_ft');
            push @row, _round($lo), _round($hi), _round($ch);
            for my $key ('temp_f', 'cond_us_cm') {
                my ($l, $h) = _window($samples, $start, $key);
                push @row, _round($l), _round($h);
            }
        }
        push @rows, \@row;
    }
    push @rows, [], ['Built ' . _et($now) . ' ET'], [$NOTE];
    return \@rows;
}

# All sheets from the archive: [{name, rows, charts}], Summary first; the
# sites in the config come first, then any other serial in the archive.
sub build {
    my ($store, $cfg, $now) = @_;
    my %known = map { $_ => 1 } @{ $cfg->{site_order} };
    my @serials = (@{ $cfg->{site_order} },
                   grep { !$known{$_} } $store->serials);
    my (@summary_in, @tabs, %used);
    $used{Summary} = 1;
    for my $serial (@serials) {
        my $reports = $store->reports($serial);
        my $samples = $store->samples($serial);
        next unless @$reports || @$samples;
        my $site = $cfg->{sites}{$serial};
        my $location = ($site && length $site->{name}) ? $site->{name}
                     : (@$reports ? $reports->[-1]{location} : '');
        push @summary_in, [$serial, $location, $samples, $reports];
        my $name = sheet_name("$serial $location" =~ s/\s+$//r);
        $name = sheet_name(substr($name, 0, 28) . '-' . scalar(keys %used))
            while $used{$name};
        $used{$name} = 1;
        push @tabs, {name => $name, rows => site_rows(daily($samples), $reports),
                     charts => \@SITE_CHARTS};
    }
    return [{name => 'Summary', rows => summary_rows(\@summary_in, $now),
             charts => []}, @tabs];
}

sub _data_rows {
    my ($rows) = @_;
    my $n = 0;
    for my $r (@$rows[1 .. $#$rows]) { last unless @$r; $n++ }
    return $n;
}

sub _col { my $i = shift; my $s = ''; $i++;
           while ($i) { my $r = ($i - 1) % 26; $s = chr(65 + $r) . $s; $i = int(($i - 1) / 26) }
           return $s }

sub write_xlsx {
    my ($sheets, $path) = @_;
    my $wb = Excel::Writer::XLSX->new($path) or die "cannot write $path: $!\n";
    my $bold = $wb->add_format(bold => 1);
    my $datef = $wb->add_format(num_format => 'yyyy-mm-dd');
    for my $sh (@$sheets) {
        my $ws = $wb->add_worksheet($sh->{name});
        my @width;
        my $rows = $sh->{rows};
        for my $i (0 .. $#$rows) {
            my $row = $rows->[$i];
            for my $j (0 .. $#$row) {
                my $v = $row->[$j];
                next unless defined $v && $v ne '';
                if (ref $v eq "HASH" && exists $v->{str}) {
                    $ws->write_string($i, $j, $v->{str});
                    $v = $v->{str};
                } elsif (ref $v eq "HASH") {
                    $ws->write_date_time($i, $j, "$v->{date}T", $datef);
                    $v = $v->{date};
                } elsif ($v =~ /^-?\d+(\.\d+)?$/) {
                    $ws->write_number($i, $j, $v, $i == 0 ? $bold : undef);
                } else {
                    $ws->write_string($i, $j, $v, $i == 0 ? $bold : undef);
                }
                my $len = length $v;
                $width[$j] = $len if $i <= 60 && $len > ($width[$j] // 0);
            }
        }
        for my $j (0 .. $#width) {
            my $w = ($width[$j] // 6) + 2;
            $ws->set_column($j, $j, $w < 8 ? 8 : $w > 40 ? 40 : $w);
        }
        $ws->freeze_panes(1, 0);
        my $n = _data_rows($rows);
        next unless $n && @{ $sh->{charts} };
        (my $q = $sh->{name}) =~ s/'/''/g;
        my $ref = sub { my ($c, $r1, $r2) = @_;
                        my $a = "'$q'!\$" . _col($c) . "\$$r1";
                        return defined $r2 ? "$a:\$" . _col($c) . "\$$r2" : $a };
        my $k = 0;
        for my $c (@{ $sh->{charts} }) {
            my ($title, $ytitle, $cols) = @$c;
            my $ch = $wb->add_chart(type => 'line', embedded => 1);
            for my $col (@$cols) {
                $ch->add_series(name => '=' . $ref->($col, 1),
                                categories => '=' . $ref->(0, 2, $n + 1),
                                values => '=' . $ref->($col, 2, $n + 1));
            }
            $ch->set_title(name => $title);
            # A date axis spaces days by date and runs oldest to newest.
            $ch->set_x_axis(name => 'Date (ET)', date_axis => 1,
                            num_format => 'mm-dd', major_unit_type => 'days');
            $ch->set_y_axis(name => $ytitle);
            $ch->set_legend(position => 'bottom');
            $ch->set_size(width => 680, height => 340);
            $ws->insert_chart(1 + $k * 18, 19, $ch);   # from column T
            $k++;
        }
    }
    $wb->close or die "cannot write $path: $!\n";
}

1;
