#!/usr/bin/env perl
# Tests for the Perl version, on the same anonymised reports and the same
# hand-worked numbers as the Python tests (../tests/test_water_monitor.py).
use strict;
use warnings;
use utf8;
use FindBin;
use lib "$FindBin::Bin/../lib";
use Email::MIME;
use File::Temp qw(tempdir);
use Test::More;
use Time::Local qw(timegm);
use WaterMonitor::Config qw(load_config parse_toml);
use WaterMonitor::Daily qw(daily missing_reports day_start_epoch);
use WaterMonitor::Intake qw(process report_from_bytes late_sites);
use WaterMonitor::Mailer qw(body_text build_message);
use WaterMonitor::Report qw(parse parse_offset to_utc_epoch);
use WaterMonitor::Store;
use WaterMonitor::Xlsx qw(build write_xlsx);

my $FIX = "$FindBin::Bin/../../tests/fixtures";
my %R = map { $_ => "$FIX/" . {52 => '100001_LS_Report_52', 57 => '100001_LS_Report_57',
              108 => '100002_LS_Report_108', 112 => '100002_LS_Report_112'}->{$_} . '.txt' }
        (52, 57, 108, 112);

sub slurp { open my $fh, '<:raw', $_[0] or die $!; local $/; <$fh> }
sub text { my $t = slurp(shift); utf8::decode($t); $t }

sub fresh {
    my $dir = tempdir(CLEANUP => 1);
    my $toml = text("$FindBin::Bin/../../water.example.toml");
    open my $fh, '>:encoding(UTF-8)', "$dir/water.toml" or die $!;
    print $fh $toml;
    close $fh;
    my $cfg = load_config("$dir/water.toml");
    mkdir "$dir/data";
    return ($cfg, WaterMonitor::Store->new($cfg->{store}), $dir);
}

subtest 'parse header, logger and samples' => sub {
    my $r = parse(text($R{57}));
    is_deeply [@$r{qw(serial location report_no)}], ['100001', 'North Well', 57];
    is_deeply [@$r{qw(battery_pct bat_v usb_v signal)}], [66, 5.07, 0, 23];
    is $r->{state}, 'reporting';
    is_deeply $r->{start_report}, [2026, 9, 16, 20, 23, 22], 'day first';
    ok !defined $r->{lat} && !defined $r->{lon}, '999 means no fix';
    is WaterMonitor::Report::logger_field($r, 'state'), 'future started';
    is WaterMonitor::Report::logger_field($r, 'total logs'), '0 of 100000';
    is scalar @{ $r->{loggers} }, 1, 'Logger 2 is NA';
    is scalar @{ $r->{samples} }, 24;
    is_deeply $r->{samples}[0]{local_time}, [2026, 11, 11, 21, 23, 22];
    is_deeply $r->{samples}[0]{values},
        {temp_f => 50.52, level_ft => 34.0518, cond_us_cm => 461, p_psi => 14.1057};
    like $r->{messages}, qr/^Email report 57, LS reporting.*L2 off/;
    is $r->{email_epoch}, timegm(19, 25, 19, 12, 10, 2026);
};

subtest 'all fixtures' => sub {
    for ([52, '100001', 69], [57, '100001', 66], [108, '100002', 49], [112, '100002', 47]) {
        my ($no, $serial, $battery) = @$_;
        my $r = parse(text($R{$no}));
        is_deeply [@$r{qw(serial report_no battery_pct)}], [$serial, $no, $battery];
        is scalar @{ $r->{samples} }, 24;
        is scalar(grep { keys %{ $_->{values} } == 4 } @{ $r->{samples} }), 24;
    }
};

subtest 'not a report' => sub {
    ok !eval { parse("Hello, this is not a report.\n") };
    like $@, qr/no LevelSender block/;
    ok !eval { parse("LevelSender\nSerial: 1\n") };
};

subtest 'offsets' => sub {
    is parse_offset('+01:00'), 3600;
    is parse_offset('-05:00'), -18000;
    is parse_offset('+0530'), 19800;
    is parse_offset('0'), 0;
    is to_utc_epoch([2026, 11, 12, 20, 23, 0], 3600), timegm(0, 23, 19, 12, 10, 2026);
};

sub eml {
    my ($body, $date, $subject) = @_;
    my $msg = Email::MIME->create(
        header_str => [From => 'LevelSender <levelsender@example.org>',
                       To => 'reports@example.org',
                       Subject => $subject // '100001 LS Report 57', Date => $date],
        attributes => {content_type => 'text/plain', charset => 'UTF-8', encoding => '8bit'},
        body_str => $body);
    return $msg->as_string;
}

subtest 'original email and forward' => sub {
    my $t = text($R{57});
    my $body = substr($t, index($t, "LevelSender\n"));
    my $r = report_from_bytes(eml($body, 'Thu, 12 Nov 2026 19:25:19 +0000'));
    is $r->{report_no}, 57;
    is $r->{email_epoch}, timegm(19, 25, 19, 12, 10, 2026);
    my $fwd = "Some note\n\n-------- Forwarded Message --------\n$t";
    $r = report_from_bytes(eml($fwd, 'Fri, 13 Nov 2026 18:15:00 +0000', 'Fwd: x'));
    is $r->{email_epoch}, timegm(19, 25, 19, 12, 10, 2026), 'the original date wins';
};

subtest 'store keeps each sample once; clock lag' => sub {
    my ($cfg, $store) = fresh();
    my $a = process(slurp($R{57}), $cfg, $store);
    my $b = process(slurp($R{57}), $cfg, $store);
    is $a->{new_samples}, 24;
    is $b->{new_samples}, 0;
    is scalar @{ $store->samples('100001') }, 24;
    my ($rep) = @{ $store->reports('100001') };
    cmp_ok abs($rep->{clock_lag_min} - (1 + 57 / 60)), '<', 1e-9;
};

subtest 'unknown site, bad mail, alerts, late' => sub {
    my ($cfg, $store) = fresh();
    delete $cfg->{sites}{100002};
    my $o = process(slurp($R{112}), $cfg, $store);
    ok !$o->{ok};
    like $o->{reason}, qr/unknown site 100002/;
    $o = process("From: a\@b\nDate: Thu, 12 Nov 2026 08:43:05 +0000\n\nhi\n", $cfg, $store);
    is $o->{reason}, 'no LevelSender block';

    ($cfg, $store) = fresh();
    $cfg->{battery_low_pct} = 50;
    process(slurp($R{52}), $cfg, $store);
    $o = process(slurp($R{57}), $cfg, $store);
    is_deeply $o->{alerts}, ['100001 North Well: report(s) 53, 54, 55, 56 never arrived'];
    $o = process(slurp($R{112}), $cfg, $store);
    is_deeply $o->{alerts}, ['100002 Creek Weir: LevelSender battery 47%'];
    is_deeply [missing_reports(108, 112, 110)], [109, 111];
    ($cfg, $store) = fresh();
    process(slurp($R{57}), $cfg, $store);
    is_deeply [late_sites($store, $cfg, timegm(0, 0, 20, 13, 10, 2026))], [];
    is_deeply [late_sites($store, $cfg, timegm(0, 0, 22, 13, 10, 2026))],
        ['100001 North Well: no report for 27 h'];
};

subtest 'daily statistics, worked by hand' => sub {
    my ($cfg, $store) = fresh();
    process(slurp($R{57}), $cfg, $store);
    my %d = map { $_->{day} => $_ } @{ daily($store->samples('100001')) };
    my $d = $d{'2026-11-12'};
    is $d->{count}, 15;
    ok $d->{partial};
    is_deeply [@{ $d->{stats}{level_ft} }{qw(min max min_at max_at)}],
        [34.0638, 34.1985, '00:23', '14:23'];
    is $d{'2026-11-11'}{count}, 9;

    process(slurp($R{112}), $cfg, $store);
    %d = map { $_->{day} => $_ } @{ daily($store->samples('100002')) };
    $d = $d{'2026-11-11'};
    is $d->{count}, 20;
    ok !$d->{partial};
    is_deeply [@{ $d->{stats}{temp_f} }{qw(min max min_at max_at)}],
        [46.575, 52.527, '04:00', '11:00'];
    is_deeply $d->{flags}, [];
};

subtest 'flags and daylight saving' => sub {
    my $ts = timegm(0, 0, 12, 12, 10, 2026);
    my ($d) = @{ daily([[$ts, {level_ft => 3, cond_us_cm => 0, temp_f => 95}],
                        [$ts + 3600, {level_ft => 4.5}]]) };
    is_deeply $d->{flags}, ['temp_f 95 high at 07:00', 'cond_us_cm 0 low at 07:00',
                            'level_ft step +1.50 at 08:00'];
    is((gmtime day_start_epoch('2026-11-12'))[2], 5, 'EST');
    is((gmtime day_start_epoch('2026-07-01'))[2], 4, 'EDT');
};

my $NOW = timegm(0, 0, 11, 13, 10, 2026);

subtest 'sheets, the .xlsx and its charts' => sub {
    my ($cfg, $store, $dir) = fresh();
    ok process(slurp($R{$_}), $cfg, $store)->{ok} for 52, 57, 108, 112;
    my $sheets = build($store, $cfg, $NOW);
    is_deeply [map { $_->{name} } @$sheets], ['Summary', '100001 North Well', '100002 Creek Weir'];
    my ($head, $a, $b) = @{ $sheets->[0]{rows} };
    my %row;
    @row{@$head} = @$a;
    is $row{Report}, 57;
    is $row{'Missing reports'}, 4;
    is $row{'Last report (ET)'}, '2026-11-12 14:25';
    is $row{'Hours since report'}, 15.6;
    my $tab = $sheets->[1]{rows};
    is_deeply [@{ $tab->[0] }[0 .. 3]], ['Date', 'Samples', 'Partial', 'Level min (ft)'];
    is $tab->[1][0]{date}, '2026-11-12', 'newest first';
    is_deeply [@{ $tab->[1] }[3, 4]], [34.0638, 34.1985];
    is_deeply [map { $tab->[0][$_] } 3, 8, 11, 14],
        ['Level min (ft)', 'Temp min (°F)', 'Cond min (µS/cm)', 'Battery (%)'];

    my $x = "$dir/w.xlsx";
    write_xlsx($sheets, $x);
    require Archive::Zip;
    my $zip = Archive::Zip->new($x);
    is scalar(grep { m{^xl/charts/chart\d+\.xml$} } $zip->memberNames), 8, 'four per site';
    my $c1 = $zip->contents('xl/charts/chart1.xml');
    like $c1, qr/'100001 North Well'!\$D\$2:\$D\$5/;
    like $c1, qr/dateAx/;

    require Spreadsheet::ParseXLSX;
    my $wb = Spreadsheet::ParseXLSX->new->parse($x);
    my $ws = $wb->worksheet('100001 North Well');
    is $ws->get_cell(1, 4)->unformatted, 34.1985;
    is $wb->worksheet('Summary')->get_cell(1, 0)->value, '100001';

    my $body = body_text($sheets->[0], $NOW);
    like $body, qr/^Water monitoring report, 2026-11-13 06:00 ET/;
    like $body, qr/last report 2026-11-12 14:25 ET \(no\. 57\), battery 66%/;
    like $body, qr/missing reports: 4/;
    my $m = build_message('r@example.org', ['a@example.org', 'b@example.org'],
                          'Water', $body, $x);
    is $m->header('To'), 'a@example.org, b@example.org';
    my @att = grep { ($_->header('Content-Disposition') // '') =~ /attachment/ } $m->subparts;
    is scalar @att, 1;
    is $att[0]->filename, 'w.xlsx';
};

subtest 'shares the archive with the Python version' => sub {
    my $toml = parse_toml(qq{[a]\nx = "1" # c\ny = [ "p", "q" ]\n[[s]]\nz = 2\n[[s]]\nz = 3\n});
    is_deeply $toml, {a => {x => '1', y => ['p', 'q']}, s => [{z => 2}, {z => 3}]};
    is WaterMonitor::Store::iso(timegm(22, 23, 20, 11, 10, 2026)), '2026-11-11T20:23:22+00:00';
    is WaterMonitor::Store::epoch_of('2026-11-11T20:23:22+00:00'), timegm(22, 23, 20, 11, 10, 2026);
};

done_testing;
