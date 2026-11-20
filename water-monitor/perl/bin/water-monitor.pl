#!/usr/bin/env perl
# The Perl version of the water monitoring intake, .xlsx only.
# Reads the same water.toml and the same SQLite archive as the Python
# version (python -m water_monitor), which also sends to ThingsBoard and
# writes the .ods.
#
#   perl bin/water-monitor.pl -c water.toml ingest report1.eml report2.txt
#   perl bin/water-monitor.pl -c water.toml poll [--every 900]
#   perl bin/water-monitor.pl -c water.toml export
#   perl bin/water-monitor.pl -c water.toml email [--to a@x.org]
#   perl bin/water-monitor.pl -c water.toml late

use strict;
use warnings;
use FindBin;
use lib "$FindBin::Bin/../lib";
use File::Copy qw(copy);
use File::Path qw(make_path);
use File::Basename qw(dirname);
use Getopt::Long qw(GetOptionsFromArray);
use WaterMonitor::Config qw(load_config);
use WaterMonitor::Daily qw(eastern);
use WaterMonitor::Intake qw(process late_sites);
use WaterMonitor::Mailer qw(body_text build_message send_message);
use WaterMonitor::Store;
use WaterMonitor::Xlsx qw(build write_xlsx);

binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';

sub export_xlsx {
    my ($cfg, $store, $now) = @_;
    my $out = $cfg->{export_dir};
    make_path("$out/archive");
    my $latest = "$out/water-report.xlsx";
    my $tmp = "$out/.water-report.xlsx.tmp";
    write_xlsx(build($store, $cfg, $now), $tmp);
    rename $tmp, $latest or die "cannot replace $latest: $!\n";
    my $stamp = eastern($now)->ymd;
    copy($latest, "$out/archive/water-report-$stamp.xlsx");
    my $cutoff = $now - $cfg->{keep_days} * 86400;
    for my $old (glob "$out/archive/water-report-*.xlsx") {
        unlink $old if (stat $old)[9] < $cutoff;
    }
    return $latest;
}

sub notify {
    print STDERR "alert: $_\n" for @_;
}

sub poll_once {
    my ($cfg, $store) = @_;
    require Mail::IMAPClient;
    my $m = $cfg->{mail};
    my $imap = Mail::IMAPClient->new(
        Server => $m->{host}, Port => $m->{port}, Ssl => 1, Uid => 1,
        User => $m->{user}, Password => $ENV{ $m->{password_env} } // '')
        or die "IMAP: $@\n";
    $imap->select($m->{folder}) or die "IMAP select: " . $imap->LastError . "\n";
    my @alerts;
    my $msgs = $imap->messages || [];
    for my $uid (@$msgs) {
        my $raw = $imap->message_string($uid) // next;
        my $out = process($raw, $cfg, $store);
        if ($out->{ok}) {
            print "report $out->{serial}/" . ($out->{report_no} // '?')
                . ": $out->{new_samples} new samples\n";
            push @alerts, @{ $out->{alerts} };
            $imap->create($m->{done_folder}) unless $imap->exists($m->{done_folder});
            $imap->move($m->{done_folder}, $uid);
        } else {
            push @alerts, "could not read an email: $out->{reason}";
            $imap->create($m->{failed_folder}) unless $imap->exists($m->{failed_folder});
            $imap->move($m->{failed_folder}, $uid);
        }
    }
    $imap->expunge;
    $imap->logout;
    notify(@alerts);
    return scalar @$msgs;
}

sub main {
    my @argv = @_;
    my $config = 'water.toml';
    Getopt::Long::Configure('require_order');
    GetOptionsFromArray(\@argv, 'c|config=s' => \$config) or return 2;
    my $cmd = shift @argv // '';
    my $cfg = load_config($config);
    make_path(dirname($cfg->{store}));
    my $store = WaterMonitor::Store->new($cfg->{store});
    my $now = time;
    my $rc = 0;
    if ($cmd eq 'ingest') {
        for my $f (@argv) {
            open my $fh, '<:raw', $f or die "cannot read $f: $!\n";
            my $out = process(do { local $/; <$fh> }, $cfg, $store);
            if ($out->{ok}) {
                print "$f: site $out->{serial} report "
                    . ($out->{report_no} // '?') . ", $out->{new_samples} new samples\n";
                print "  alert: $_\n" for @{ $out->{alerts} };
            } else {
                print STDERR "$f: not read: $out->{reason}\n";
                $rc = 1;
            }
        }
    } elsif ($cmd eq 'poll') {
        my $every = 0;
        GetOptionsFromArray(\@argv, 'every=i' => \$every) or return 2;
        while (1) {
            poll_once($cfg, $store);
            last unless $every;
            sleep $every;
        }
    } elsif ($cmd eq 'export') {
        print export_xlsx($cfg, $store, $now), "\n";
    } elsif ($cmd eq 'email') {
        my @to;
        GetOptionsFromArray(\@argv, 'to=s' => \@to) or return 2;
        my $e = $cfg->{email};
        @to = @{ $e->{to} } unless @to;
        unless ($e->{smtp_host} && $e->{sender} && @to) {
            print STDERR "set smtp_host, sender and to in [email]\n";
            return 2;
        }
        my $file = export_xlsx($cfg, $store, $now);
        my $summary = build($store, $cfg, $now)->[0];
        my $msg = build_message($e->{sender}, \@to,
            "$e->{subject}, " . eastern($now)->ymd, body_text($summary, $now), $file);
        send_message($msg, $e, \@to);
        print 'sent to ', join(', ', @to), "\n";
    } elsif ($cmd eq 'late') {
        notify(late_sites($store, $cfg, $now));
    } else {
        print STDERR "usage: water-monitor.pl [-c water.toml] "
            . "ingest FILES | poll [--every N] | export | email [--to ADDR] | late\n";
        $rc = 2;
    }
    $store->close;
    return $rc;
}

exit main(@ARGV) unless caller;
1;
